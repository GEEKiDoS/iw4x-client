#include <STDInclude.hpp>

namespace Components
{
	std::string Script::ScriptName;
	std::vector<int> Script::ScriptHandles;
	std::unordered_map<std::string, Game::BuiltinFunctionDef> Script::CustomScrFunctions;
	std::unordered_map<std::string, Game::BuiltinMethodDef> Script::CustomScrMethods;
	std::vector<std::string> Script::ScriptNameStack;
	unsigned short Script::FunctionName;
	std::unordered_map<std::string, std::string> Script::ScriptStorage;
	std::unordered_map<int, std::string> Script::ScriptBaseProgramNum;
	std::unordered_map<const char*, const char*> Script::ReplacedFunctions;
	const char* Script::ReplacedPos = nullptr;
	int Script::LastFrameTime = -1;

	Utils::Signal<Scheduler::Callback> Script::VMShutdownSignal;

	void Script::FunctionError()
	{
		const auto* funcName = Game::SL_ConvertToString(Script::FunctionName);

		Game::Scr_ShutdownAllocNode();

		Logger::Print(23, "\n");
		Logger::Print(23, "******* script compile error *******\n");
		Logger::Print(23, "Error: unknown function %s in %s\n", funcName, Script::ScriptName.data());
		Logger::Print(23, "************************************\n");

		Logger::Error(Game::ERR_SCRIPT_DROP, "script compile error\nunknown function %s\n%s\n\n", funcName, Script::ScriptName.data());
	}

	__declspec(naked) void Script::StoreFunctionNameStub()
	{
		__asm
		{
			mov eax, [esp - 8h]
			mov Script::FunctionName, ax

			sub esp, 0Ch
			push 0
			push edi

			mov eax, 612DB6h
			jmp eax
		}
	}

	void Script::RuntimeError(const char* codePos, unsigned int index, const char* msg, const char* dialogMessage)
	{
		const auto developer = Dvar::Var("developer").get<int>();

		// Allow error messages to be printed if developer mode is on
		// Should check scrVarPub.developer but it's absent
		// in this version of the game so let's check the dvar
		if (!Game::scrVmPub->terminal_error && !developer)
			return;

		// If were are developing let's call RuntimeErrorInternal
		// scrVmPub.debugCode seems to be always false
		if (Game::scrVmPub->debugCode || Game::scrVarPub->developer_script)
		{
			Game::RuntimeErrorInternal(23, codePos, index, msg);
		}
		else
		{
			Logger::Print(23, "%s\n", msg);
		}

		// Let's not throw error unless we have to
		if (Game::scrVmPub->terminal_error)
		{
			if (dialogMessage == nullptr)
				dialogMessage = "";

			Logger::Error(Game::ERR_SCRIPT_DROP, "\x15script runtime error\n(see console for details)\n%s\n%s", msg, dialogMessage);
		}
	}

	void Script::StoreScriptName(const char* name)
	{
		Script::ScriptNameStack.push_back(Script::ScriptName);
		Script::ScriptName = name;

		if (!Utils::String::EndsWith(Script::ScriptName, ".gsc"))
		{
			Script::ScriptName.append(".gsc");
		}
	}

	__declspec(naked) void Script::StoreScriptNameStub()
	{
		__asm
		{
			pushad

			lea ecx, [esp + 30h]
			push ecx

			call Script::StoreScriptName
			add esp, 4h

			popad

			push ebp
			mov ebp, ds:1CDEAA8h

			push 427DC3h
			retn
		}
	}

	void Script::RestoreScriptName()
	{
		Script::ScriptName = Script::ScriptNameStack.back();
		Script::ScriptNameStack.pop_back();
	}

	__declspec(naked) void Script::RestoreScriptNameStub()
	{
		__asm
		{
			pushad
			call Script::RestoreScriptName
			popad

			mov ds:1CDEAA8h, ebp

			push 427E77h
			retn
		}
	}

	void Script::PrintSourcePos(const char* filename, unsigned int offset)
	{
		FileSystem::File script(filename);

		if (script.exists())
		{
			std::string buffer = script.getBuffer();
			Utils::String::Replace(buffer, "\t", " ");

			auto line = 1, lineOffset = 0, inlineOffset = 0;

			for (size_t i = 0; i < buffer.size(); ++i)
			{
				// Terminate line
				if (i == offset)
				{
					while (buffer[i] != '\r' && buffer[i] != '\n' && buffer[i] != '\0')
					{
						++i;
					}

					buffer[i] = '\0';
					break;
				}

				if (buffer[i] == '\n')
				{
					++line;
					lineOffset = static_cast<int>(i); // Includes the line break!
					inlineOffset = 0;
				}
				else
				{
					++inlineOffset;
				}
			}

			Logger::Print(23, "in file %s, line %d:", filename, line);
			Logger::Print(23, "%s\n", buffer.data() + lineOffset);

			for (auto i = 0; i < (inlineOffset - 1); ++i)
			{
				Logger::Print(23, " ");
			}

			Logger::Print(23, "*\n");
		}
		else
		{
			Logger::Print(23, "in file %s, offset %d\n", filename, offset);
		}
	}

	void Script::CompileError(unsigned int offset, const char* message, ...)
	{
		char msgbuf[1024] = {0};
		va_list va;
		va_start(va, message);
		_vsnprintf_s(msgbuf, _TRUNCATE, message, va);
		va_end(va);

		Game::Scr_ShutdownAllocNode();

		Logger::Print(23, "\n");
		Logger::Print(23, "******* script compile error *******\n");
		Logger::Print(23, "Error: %s ", msgbuf);
		Script::PrintSourcePos(Script::ScriptName.data(), offset);
		Logger::Print(23, "************************************\n\n");

		Logger::Error(Game::ERR_SCRIPT_DROP, "script compile error\n%s\n%s\n(see console for actual details)\n", msgbuf, Script::ScriptName.data());
	}

	int Script::LoadScriptAndLabel(const std::string& script, const std::string& label)
	{
		Logger::Print("Loading script %s.gsc...\n", script.data());

		if (!Game::Scr_LoadScript(script.data()))
		{
			Logger::Print("Script %s encountered an error while loading. (doesn't exist?)", script.data());
			Logger::Error(Game::ERR_DROP, reinterpret_cast<const char*>(0x70B810), script.data());
		}
		else
		{
			Logger::Print("Script %s.gsc loaded successfully.\n", script.data());
		}

		Logger::Print("Finding script handle %s::%s...\n", script.data(), label.data());
		const auto handle = Game::Scr_GetFunctionHandle(script.data(), label.data());
		if (handle)
		{
			Logger::Print("Script handle %s::%s loaded successfully.\n", script.data(), label.data());
			return handle;
		}

		Logger::Print("Script handle %s::%s couldn't be loaded. (file with no entry point?)\n", script.data(), label.data());
		return handle;
	}

	void Script::LoadGameType()
	{
		for (const auto& handle : Script::ScriptHandles)
		{
			Game::Scr_FreeThread(Game::Scr_ExecThread(handle, 0));
		}

		Game::Scr_LoadGameType();
	}

	void Script::LoadGameTypeScript()
	{
		Script::ScriptHandles.clear();

		const auto list = FileSystem::GetFileList("scripts/", "gsc");

		for (auto file : list)
		{
			file.insert(0, "scripts/");

			if (Utils::String::EndsWith(file, ".gsc"))
			{
				file = file.substr(0, file.size() - 4);
			}

			auto handle = Script::LoadScriptAndLabel(file, "init");

			if (handle)
			{
				Script::ScriptHandles.push_back(handle);
			}
			else
			{
				handle = Script::LoadScriptAndLabel(file, "main");
				if (handle) Script::ScriptHandles.push_back(handle);
			}
		}

		Game::GScr_LoadGameTypeScript();
	}

	void Script::AddFunction(const char* name, Game::BuiltinFunction func, int type)
	{
		Game::BuiltinFunctionDef toAdd;
		toAdd.actionString = name;
		toAdd.actionFunc = func;
		toAdd.type = type;

		CustomScrFunctions.insert_or_assign(Utils::String::ToLower(name), toAdd);
	}

	void Script::AddMethod(const char* name, Game::BuiltinMethod func, int type)
	{
		Game::BuiltinMethodDef toAdd;
		toAdd.actionString = name;
		toAdd.actionFunc = func;
		toAdd.type = type;

		CustomScrMethods.insert_or_assign(Utils::String::ToLower(name), toAdd);
	}

	Game::BuiltinFunction Script::BuiltIn_GetFunctionStub(const char** pName, int* type)
	{
		if (pName != nullptr)
		{
			const auto got = Script::CustomScrFunctions.find(Utils::String::ToLower(*pName));

			// If no function was found let's call game's function
			if (got != Script::CustomScrFunctions.end())
			{
				*type = got->second.type;
				return got->second.actionFunc;
			}			
		}
		else
		{
			for (const auto& [name, builtin] : Script::CustomScrFunctions)
			{
				Game::Scr_RegisterFunction(reinterpret_cast<int>(builtin.actionFunc), name.data());
			}
		}

		return Utils::Hook::Call<Game::BuiltinFunction(const char**, int*)>(0x5FA2B0)(pName, type); // BuiltIn_GetFunction
	}

	Game::BuiltinMethod Script::BuiltIn_GetMethod(const char** pName, int* type)
	{
		if (pName != nullptr)
		{
			const auto got = Script::CustomScrMethods.find(Utils::String::ToLower(*pName));

			// If no method was found let's call game's function
			if (got != Script::CustomScrMethods.end())
			{
				*type = got->second.type;
				return got->second.actionFunc;
			}
		}
		else
		{
			for (const auto& [name, builtin] : Script::CustomScrMethods)
			{
				Game::Scr_RegisterFunction(reinterpret_cast<int>(builtin.actionFunc), name.data());
			}
		}

		return Utils::Hook::Call<Game::BuiltinMethod(const char**, int*)>(0x5FA360)(pName, type); // Player_GetMethod
	}

	void Script::StoreScriptBaseProgramNum()
	{
		Script::ScriptBaseProgramNum.insert_or_assign(Utils::Hook::Get<int>(0x1CFEEF8), Script::ScriptName);
	}

	void Script::Scr_PrintPrevCodePos(int scriptPos)
	{
		auto bestCodePos = -1, nextCodePos = -1, offset = -1;
		std::string file;

		for (const auto& [key, value] : Script::ScriptBaseProgramNum)
		{
			const auto codePos = key;

			if (codePos > scriptPos)
			{
				if (nextCodePos == -1 || codePos < nextCodePos)
					nextCodePos = codePos;

				continue;
			}

			if (codePos < bestCodePos)
				continue;

			bestCodePos = codePos;

			file = value;
			offset = scriptPos - bestCodePos;
		}

		if (bestCodePos == -1)
			return;

		Logger::Print(23, "\n@ %d (%d - %d)\n", scriptPos, bestCodePos, nextCodePos);
		Logger::Print(23, "in %s (%.1f%% through the source)\n\n", file.data(), ((offset * 100.0f) / (nextCodePos - bestCodePos)));
	}

	__declspec(naked) void Script::Scr_PrintPrevCodePosStub()
	{
		__asm
		{
			push esi
			call Script::Scr_PrintPrevCodePos
			add esp, 4h

			pop esi
			retn
		}
	}

	__declspec(naked) void Script::StoreScriptBaseProgramNumStub()
	{
		__asm
		{
			// execute our hook
			pushad

			call Script::StoreScriptBaseProgramNum

			popad

			// execute overwritten code caused by the jump hook
			sub     eax, ds:201A460h // gScrVarPub_programBuffer
			add     esp, 0Ch
			mov     ds : 1CFEEF8h, eax // gScrCompilePub_programLen

			// jump back to the original code
			push    426C3Bh
			retn
		}
	}

	void Script::OnVMShutdown(Utils::Slot<Scheduler::Callback> callback)
	{
		Script::ScriptBaseProgramNum.clear();
		Script::VMShutdownSignal.connect(std::move(callback));
	}

	void Script::ScrShutdownSystemStub(unsigned char sys)
	{
		Script::VMShutdownSignal();

		// Scr_ShutdownSystem
		Utils::Hook::Call<void(unsigned char)>(0x421EE0)(sys);
	}

	unsigned int Script::SetExpFogStub()
	{
		if (Game::Scr_GetNumParam() == 6u)
		{
			std::memmove(&Game::scrVmPub->top[-4], &Game::scrVmPub->top[-5], sizeof(Game::VariableValue) * 6);
			Game::scrVmPub->top += 1;
			Game::scrVmPub->top[-6].type = Game::scrParamType_t::VAR_FLOAT;
			Game::scrVmPub->top[-6].u.floatValue = 0.0f;

			++Game::scrVmPub->outparamcount;
		}

		return Game::Scr_GetNumParam();
	}

	const char* Script::GetCodePosForParam(int index)
	{
		if (static_cast<unsigned int>(index) >= Game::scrVmPub->outparamcount)
		{
			Game::Scr_ParamError(static_cast<unsigned int>(index), "^1GetCodePosForParam: Index is out of range!\n");
			return "";
		}

		const auto value = &Game::scrVmPub->top[-index];

		if (value->type != Game::scrParamType_t::VAR_FUNCTION)
		{
			Game::Scr_ParamError(static_cast<unsigned int>(index), "^1GetCodePosForParam: Expects a function as parameter!\n");
			return "";
		}

		return value->u.codePosValue;
	}

	void Script::GetReplacedPos(const char* pos)
	{
		if (Script::ReplacedFunctions.find(pos) != Script::ReplacedFunctions.end())
		{
			Script::ReplacedPos = Script::ReplacedFunctions[pos];
		}
	}

	void Script::SetReplacedPos(const char* what, const char* with)
	{
		if (what[0] == '\0' || with[0] == '\0')
		{
			Logger::Print("Warning: Invalid parameters passed to ReplacedFunctions\n");
			return;
		}

		if (Script::ReplacedFunctions.find(what) != Script::ReplacedFunctions.end())
		{
			Logger::Print("Warning: ReplacedFunctions already contains codePosValue for a function\n");
		}

		Script::ReplacedFunctions[what] = with;
	}

	__declspec(naked) void Script::VMExecuteInternalStub()
	{
		__asm
		{
			pushad

			push edx
			call Script::GetReplacedPos

			pop edx
			popad

			cmp Script::ReplacedPos, 0
			jne SetPos

			movzx eax, byte ptr [edx]
			inc edx

		Loc1:
			cmp eax, 0x8B

			push ecx

			mov ecx, 0x2045094
			mov [ecx], eax

			mov ecx, 0x2040CD4
			mov [ecx], edx

			pop ecx

			push 0x61E944
			retn

		SetPos:
			mov edx, Script::ReplacedPos
			mov Script::ReplacedPos, 0

			movzx eax, byte ptr [edx]
			inc edx

			jmp Loc1
		}
	}

	Game::client_t* Script::GetClient(const Game::gentity_t* ent)
	{
		assert(ent != nullptr);

		if (ent->client == nullptr)
		{
			Game::Scr_ObjectError(Utils::String::VA("Entity %i is not a player", ent->s.number));
			return nullptr;
		}

		if (ent->s.number >= *Game::svs_clientCount)
		{
			Game::Scr_ObjectError(Utils::String::VA("Entity %i is out of bounds", ent->s.number));
			return nullptr;
		}

		return &Game::svs_clients[ent->s.number];
	}

	void Script::AddFunctions()
	{
		Script::AddFunction("ReplaceFunc", [] // gsc: ReplaceFunc(<function>, <function>)
		{
			if (Game::Scr_GetNumParam() != 2u)
			{
				Game::Scr_Error("^1ReplaceFunc: Needs two parameters!\n");
				return;
			}

			const auto what = Script::GetCodePosForParam(0);
			const auto with = Script::GetCodePosForParam(1);

			Script::SetReplacedPos(what, with);
		});

		// System time
		Script::AddFunction("GetSystemMilliseconds", [] // gsc: GetSystemMilliseconds()
		{
			SYSTEMTIME time;
			GetSystemTime(&time);

			Game::Scr_AddInt(time.wMilliseconds);
		});

		// Executes command to the console
		Script::AddFunction("Exec", [] // gsc: Exec(<string>)
		{
			const auto str = Game::Scr_GetString(0);

			if (str == nullptr)
			{
				Game::Scr_ParamError(0, "^1Exec: Illegal parameter!\n");
				return;
			}

			Command::Execute(str, false);
		});

		// Allow printing to the console even when developer is 0
		Script::AddFunction("PrintConsole", [] // gsc: PrintConsole(<string>)
		{
			for (auto i = 0u; i < Game::Scr_GetNumParam(); i++)
			{
				const auto str = Game::Scr_GetString(i);

				if (str == nullptr)
				{
					Game::Scr_ParamError(i, "^1PrintConsole: Illegal parameter!\n");
					return;
				}

				Logger::Print(Game::level->scriptPrintChannel, "%s", str);
			}
		});

		// Script Storage Functions
		Script::AddFunction("StorageSet", [] // gsc: StorageSet(<str key>, <str data>);
		{
			const auto* key = Game::Scr_GetString(0);
			const auto* value = Game::Scr_GetString(1);

			if (key == nullptr || value == nullptr)
			{
				Game::Scr_Error("^1StorageSet: Illegal parameters!\n");
				return;
			}

			Script::ScriptStorage.insert_or_assign(key, value);
		});

		Script::AddFunction("StorageRemove", [] // gsc: StorageRemove(<str key>);
		{
			const auto* key = Game::Scr_GetString(0);

			if (key == nullptr)
			{
				Game::Scr_Error("^1StorageRemove: Illegal parameter!\n");
				return;
			}

			if (!Script::ScriptStorage.count(key))
			{
				Game::Scr_Error(Utils::String::VA("^1StorageRemove: Store does not have key '%s'!\n", key));
				return;
			}

			Script::ScriptStorage.erase(key);
		});

		Script::AddFunction("StorageGet", [] // gsc: StorageGet(<str key>);
		{
			const auto* key = Game::Scr_GetString(0);

			if (key == nullptr)
			{
				Game::Scr_Error("^1StorageGet: Illegal parameter!\n");
				return;
			}

			if (!Script::ScriptStorage.count(key))
			{
				Game::Scr_Error(Utils::String::VA("^1StorageGet: Store does not have key '%s'!\n", key));
				return;
			}

			const auto& data = Script::ScriptStorage.at(key);
			Game::Scr_AddString(data.data());
		});

		Script::AddFunction("StorageHas", [] // gsc: StorageHas(<str key>);
		{
			const auto* key = Game::Scr_GetString(0);

			if (key == nullptr)
			{
				Game::Scr_Error("^1StorageHas: Illegal parameter!\n");
				return;
			}

			Game::Scr_AddBool(static_cast<int>(Script::ScriptStorage.count(key))); // Until C++17
		});

		Script::AddFunction("StorageClear", [] // gsc: StorageClear();
		{
			Script::ScriptStorage.clear();
		});

		// PlayerCmd_AreControlsFrozen GSC function from Black Ops 2
		Script::AddMethod("AreControlsFrozen", [](Game::scr_entref_t entref) // Usage: self AreControlsFrozen();
		{
			const auto* ent = Game::GetPlayerEntity(entref);

			Game::Scr_AddBool((ent->client->flags & Game::PLAYER_FLAG_FROZEN) != 0);
		});
	}

	Script::Script()
	{
		Utils::Hook(0x612DB0, Script::StoreFunctionNameStub, HOOK_JUMP).install()->quick();
		Utils::Hook(0x427E71, Script::RestoreScriptNameStub, HOOK_JUMP).install()->quick();
		Utils::Hook(0x427DBC, Script::StoreScriptNameStub, HOOK_JUMP).install()->quick();
		Utils::Hook(0x426C2D, Script::StoreScriptBaseProgramNumStub, HOOK_JUMP).install()->quick();
		Utils::Hook(0x42281B, Script::Scr_PrintPrevCodePosStub, HOOK_JUMP).install()->quick();

		Utils::Hook(0x61E3AD, Script::RuntimeError, HOOK_CALL).install()->quick();
		Utils::Hook(0x621976, Script::RuntimeError, HOOK_CALL).install()->quick();
		Utils::Hook(0x62246E, Script::RuntimeError, HOOK_CALL).install()->quick();
		// Skip check in GScr_CheckAllowedToSetPersistentData to prevent log spam in RuntimeError.
		// On IW5 the function is entirely nullsubbed
		Utils::Hook::Set<BYTE>(0x5F8DBF, 0xEB);

		Utils::Hook(0x612E8D, Script::FunctionError, HOOK_CALL).install()->quick();
		Utils::Hook(0x612EA2, Script::FunctionError, HOOK_CALL).install()->quick();
		Utils::Hook(0x434260, Script::CompileError, HOOK_JUMP).install()->quick();

		Utils::Hook(0x48EFFE, Script::LoadGameType, HOOK_CALL).install()->quick();
		Utils::Hook(0x45D44A, Script::LoadGameTypeScript, HOOK_CALL).install()->quick();

		// Fetch custom functions
		Utils::Hook(0x44E72E, Script::BuiltIn_GetFunctionStub, HOOK_CALL).install()->quick(); // Scr_GetFunction
		Utils::Hook(0x4EC8DD, Script::BuiltIn_GetMethod, HOOK_CALL).install()->quick(); // Scr_GetMethod

		Utils::Hook(0x5F41A3, Script::SetExpFogStub, HOOK_CALL).install()->quick();

		Utils::Hook(0x61E92E, Script::VMExecuteInternalStub, HOOK_JUMP).install()->quick();
		Utils::Hook::Nop(0x61E933, 1);

		Utils::Hook(0x47548B, Script::ScrShutdownSystemStub, HOOK_CALL).install()->quick(); // G_LoadGame
		Utils::Hook(0x4D06BA, Script::ScrShutdownSystemStub, HOOK_CALL).install()->quick(); // G_ShutdownGame

		Scheduler::OnFrame([]()
		{
			if (!Game::SV_Loaded())
				return;

			const auto nowMs = Game::Sys_Milliseconds();

			if (Script::LastFrameTime != -1)
			{
				const auto timeScale = Dvar::Var("timescale").get<float>();
				const auto timeTaken = static_cast<int>((nowMs - Script::LastFrameTime) * timeScale);

				if (timeTaken >= 500)
					Logger::Print(23, "Hitch warning: %i msec frame time\n", timeTaken);
			}

			Script::LastFrameTime = nowMs;
		});

#ifdef _DEBUG 
		Script::AddFunction("DebugBox", []
		{
			const auto* message = Game::Scr_GetString(0);

			if (message == nullptr)
			{
				Game::Scr_Error("^1DebugBox: Illegal parameter!\n");
			}

			MessageBoxA(nullptr, message, "DEBUG", MB_OK);
		}, 1);
#endif

		Script::AddFunctions();

		Script::OnVMShutdown([]
		{
			Script::ReplacedFunctions.clear();
		});
	}

	Script::~Script()
	{
		Script::VMShutdownSignal.clear();
	}
}
