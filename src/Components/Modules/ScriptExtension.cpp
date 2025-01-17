#include <STDInclude.hpp>

namespace Components
{
	const char* ScriptExtension::QueryStrings[] = { R"(..)", R"(../)", R"(..\)" };

	std::unordered_map<std::uint16_t, Game::ent_field_t> ScriptExtension::CustomEntityFields;
	std::unordered_map<std::uint16_t, Game::client_fields_s> ScriptExtension::CustomClientFields;

	void ScriptExtension::AddEntityField(const char* name, Game::fieldtype_t type,
		const Game::ScriptCallbackEnt& setter, const Game::ScriptCallbackEnt& getter)
	{
		static std::uint16_t fieldOffsetStart = 15; // fields count
		assert((fieldOffsetStart & Game::ENTFIELD_MASK) == Game::ENTFIELD_ENTITY);

		ScriptExtension::CustomEntityFields[fieldOffsetStart] = {name, fieldOffsetStart, type, setter, getter};
		++fieldOffsetStart;
	}

	void ScriptExtension::AddClientField(const char* name, Game::fieldtype_t type,
		const Game::ScriptCallbackClient& setter, const Game::ScriptCallbackClient& getter)
	{
		static std::uint16_t fieldOffsetStart = 21; // fields count
		assert((fieldOffsetStart & Game::ENTFIELD_MASK) == Game::ENTFIELD_ENTITY);

		const auto offset = fieldOffsetStart | Game::ENTFIELD_CLIENT; // This is how client field's offset is calculated

		// Use 'index' in 'array' as map key. It will be used later in Scr_SetObjectFieldStub
		ScriptExtension::CustomClientFields[fieldOffsetStart] = {name, offset, type, setter, getter};
		++fieldOffsetStart;
	}

	void ScriptExtension::GScr_AddFieldsForEntityStub()
	{
		for (const auto& [offset, field] : ScriptExtension::CustomEntityFields)
		{
			Game::Scr_AddClassField(Game::ClassNum::CLASS_NUM_ENTITY, field.name, field.ofs);
		}

		Utils::Hook::Call<void()>(0x4A7CF0)(); // GScr_AddFieldsForClient

		for (const auto& [offset, field] : ScriptExtension::CustomClientFields)
		{
			Game::Scr_AddClassField(Game::ClassNum::CLASS_NUM_ENTITY, field.name, field.ofs);
		}
	}

	// Because some functions are inlined we have to hook this function instead of Scr_SetEntityField
	int ScriptExtension::Scr_SetObjectFieldStub(unsigned int classnum, int entnum, int offset)
	{
		if (classnum == Game::ClassNum::CLASS_NUM_ENTITY)
		{
			const auto entity_offset = static_cast<std::uint16_t>(offset);

			const auto got = ScriptExtension::CustomEntityFields.find(entity_offset);
			if (got != ScriptExtension::CustomEntityFields.end())
			{
				got->second.setter(&Game::g_entities[entnum], offset);
				return 1;
			}
		}

		// No custom generic field was found, let the game handle it
		return Game::Scr_SetObjectField(classnum, entnum, offset);
	}

	// Offset was already converted to array 'index' following binop offset & ~Game::ENTFIELD_MASK
	void ScriptExtension::Scr_SetClientFieldStub(Game::gclient_s* client, int offset)
	{
		const auto client_offset = static_cast<std::uint16_t>(offset);

		const auto got = ScriptExtension::CustomClientFields.find(client_offset);
		if (got != ScriptExtension::CustomClientFields.end())
		{
			got->second.setter(client, &got->second);
			return;
		}

		// No custom field client was found, let the game handle it
		Game::Scr_SetClientField(client, offset);
	}

	void ScriptExtension::Scr_GetEntityFieldStub(int entnum, int offset)
	{
		if ((offset & Game::ENTFIELD_MASK) == Game::ENTFIELD_CLIENT)
		{
			// If we have a ENTFIELD_CLIENT offset we need to check g_entity is actually a fully connected client
			if (Game::g_entities[entnum].client != nullptr)
			{
				const auto client_offset = static_cast<std::uint16_t>(offset & ~Game::ENTFIELD_MASK);

				const auto got = ScriptExtension::CustomClientFields.find(client_offset);
				if (got != ScriptExtension::CustomClientFields.end())
				{
					// Game functions probably don't ever need to use the reference to client_fields_s...
					got->second.getter(Game::g_entities[entnum].client, &got->second);
					return;
				}
			}
		}

		// Regular entity offsets can be searched directly in our custom handler
		const auto entity_offset = static_cast<std::uint16_t>(offset);

		const auto got = ScriptExtension::CustomEntityFields.find(entity_offset);
		if (got != ScriptExtension::CustomEntityFields.end())
		{
			got->second.getter(&Game::g_entities[entnum], offset);
			return;
		}

		// No custom generic field was found, let the game handle it
		Game::Scr_GetEntityField(entnum, offset);
	}

	void ScriptExtension::AddFunctions()
	{
		// File functions
		Script::AddFunction("FileWrite", [] // gsc: FileWrite(<filepath>, <string>, <mode>)
		{
			const auto* path = Game::Scr_GetString(0);
			auto* text = Game::Scr_GetString(1);
			auto* mode = Game::Scr_GetString(2);

			if (path == nullptr)
			{
				Game::Scr_ParamError(0, "^1FileWrite: filepath is not defined!\n");
				return;
			}

			if (text == nullptr || mode == nullptr)
			{
				Game::Scr_Error("^1FileWrite: Illegal parameters!\n");
				return;
			}

			for (auto i = 0u; i < ARRAYSIZE(ScriptExtension::QueryStrings); ++i)
			{
				if (std::strstr(path, ScriptExtension::QueryStrings[i]) != nullptr)
				{
					Logger::Print("^1FileWrite: directory traversal is not allowed!\n");
					return;
				}
			}

			if (mode != "append"s && mode != "write"s)
			{
				Logger::Print("^3FileWrite: mode not defined or was wrong, defaulting to 'write'\n");
				mode = "write";
			}

			if (mode == "write"s)
			{
				FileSystem::FileWriter(path).write(text);
			}
			else if (mode == "append"s)
			{
				FileSystem::FileWriter(path, true).write(text);
			}
		});

		Script::AddFunction("FileRead", [] // gsc: FileRead(<filepath>)
		{
			const auto* path = Game::Scr_GetString(0);

			if (path == nullptr)
			{
				Game::Scr_ParamError(0, "^1FileRead: filepath is not defined!\n");
				return;
			}

			for (auto i = 0u; i < ARRAYSIZE(ScriptExtension::QueryStrings); ++i)
			{
				if (std::strstr(path, ScriptExtension::QueryStrings[i]) != nullptr)
				{
					Logger::Print("^1FileRead: directory traversal is not allowed!\n");
					return;
				}
			}

			if (!FileSystem::FileReader(path).exists())
			{
				Logger::Print("^1FileRead: file not found!\n");
				return;
			}

			Game::Scr_AddString(FileSystem::FileReader(path).getBuffer().data());
		});

		Script::AddFunction("FileExists", [] // gsc: FileExists(<filepath>)
		{
			const auto* path = Game::Scr_GetString(0);

			if (path == nullptr)
			{
				Game::Scr_ParamError(0, "^1FileExists: filepath is not defined!\n");
				return;
			}

			for (auto i = 0u; i < ARRAYSIZE(ScriptExtension::QueryStrings); ++i)
			{
				if (std::strstr(path, ScriptExtension::QueryStrings[i]) != nullptr)
				{
					Logger::Print("^1FileExists: directory traversal is not allowed!\n");
					return;
				}
			}

			Game::Scr_AddInt(FileSystem::FileReader(path).exists());
		});

		Script::AddFunction("FileRemove", [] // gsc: FileRemove(<filepath>)
		{
			const auto* path = Game::Scr_GetString(0);

			if (path == nullptr)
			{
				Game::Scr_ParamError(0, "^1FileRemove: filepath is not defined!\n");
				return;
			}

			for (auto i = 0u; i < ARRAYSIZE(ScriptExtension::QueryStrings); ++i)
			{
				if (std::strstr(path, ScriptExtension::QueryStrings[i]) != nullptr)
				{
					Logger::Print("^1FileRemove: directory traversal is not allowed!\n");
					return;
				}
			}

			const auto p = std::filesystem::path(path);
			const auto& folder = p.parent_path().string();
			const auto& file = p.filename().string();
			Game::Scr_AddInt(FileSystem::DeleteFile(folder, file));
		});

		// Misc functions
		Script::AddFunction("ToUpper", [] // gsc: ToUpper(<string>)
		{
			const auto scriptValue = Game::Scr_GetConstString(0);
			const auto* string = Game::SL_ConvertToString(scriptValue);

			char out[1024] = {0}; // 1024 is the max for a string in this SL system
			bool changed = false;

			size_t i = 0;
			while (i < sizeof(out))
			{
				const auto value = *string;
				const auto result = static_cast<char>(std::toupper(static_cast<unsigned char>(value)));
				out[i] = result;

				if (value != result)
					changed = true;

				if (result == '\0') // Finished converting string
					break;

				++string;
				++i;
			}

			// Null terminating character was overwritten 
			if (i >= sizeof(out))
			{
				Game::Scr_Error("string too long");
				return;
			}

			if (changed)
			{
				Game::Scr_AddString(out);
			}
			else
			{
				Game::SL_AddRefToString(scriptValue);
				Game::Scr_AddConstString(scriptValue);
				Game::SL_RemoveRefToString(scriptValue);
			}
		});

		// Func present on IW5
		Script::AddFunction("StrICmp", [] // gsc: StrICmp(<string>, <string>)
		{
			const auto value1 = Game::Scr_GetConstString(0);
			const auto value2 = Game::Scr_GetConstString(1);

			const auto result = _stricmp(Game::SL_ConvertToString(value1),
				Game::SL_ConvertToString(value2));

			Game::Scr_AddInt(result);
		});

		// Func present on IW5
		Script::AddFunction("IsEndStr", [] // gsc: IsEndStr(<string>, <string>)
		{
			const auto* s1 = Game::Scr_GetString(0);
			const auto* s2 = Game::Scr_GetString(1);

			if (s1 == nullptr || s2 == nullptr)
			{
				Game::Scr_Error("^1IsEndStr: Illegal parameters!\n");
				return;
			}

			Game::Scr_AddBool(Utils::String::EndsWith(s1, s2));
		});

		Script::AddFunction("IsArray", [] // gsc: IsArray(<object>)
		{
			const auto type = Game::Scr_GetType(0);

			bool result;
			if (type == Game::scrParamType_t::VAR_POINTER)
			{
				const auto ptr_type = Game::Scr_GetPointerType(0);
				assert(ptr_type >= Game::FIRST_OBJECT);
				result = (ptr_type == Game::scrParamType_t::VAR_ARRAY);
			}
			else
			{
				assert(type < Game::FIRST_OBJECT);
				result = false;
			}

			Game::Scr_AddBool(result);
		});
	}

	void ScriptExtension::AddMethods()
	{
		// ScriptExtension methods
		Script::AddMethod("GetIp", [](Game::scr_entref_t entref) // gsc: self GetIp()
		{
			const auto* ent = Game::GetPlayerEntity(entref);
			const auto* client = Script::GetClient(ent);

			std::string ip = Game::NET_AdrToString(client->netchan.remoteAddress);

			if (const auto pos = ip.find_first_of(":"); pos != std::string::npos)
				ip.erase(ip.begin() + pos, ip.end()); // Erase port

			Game::Scr_AddString(ip.data());
		});

		Script::AddMethod("GetPing", [](Game::scr_entref_t entref) // gsc: self GetPing()
		{
			const auto* ent = Game::GetPlayerEntity(entref);
			const auto* client = Script::GetClient(ent);

			Game::Scr_AddInt(client->ping);
		});
	}

	void ScriptExtension::Scr_TableLookupIStringByRow()
	{
		if (Game::Scr_GetNumParam() < 3)
		{
			Game::Scr_Error("USAGE: tableLookupIStringByRow( filename, rowNum, returnValueColumnNum )\n");
			return;
		}

		const auto* fileName = Game::Scr_GetString(0);
		const auto rowNum = Game::Scr_GetInt(1);
		const auto returnValueColumnNum = Game::Scr_GetInt(2);

		const auto* table = Game::DB_FindXAssetHeader(Game::ASSET_TYPE_STRINGTABLE, fileName).stringTable;

		if (table == nullptr)
		{
			Game::Scr_ParamError(0, Utils::String::VA("%s does not exist\n", fileName));
			return;
		}

		const auto* value = Game::StringTable_GetColumnValueForRow(table, rowNum, returnValueColumnNum);
		Game::Scr_AddIString(value);
	}

	void ScriptExtension::AddEntityFields()
	{
		ScriptExtension::AddEntityField("entityflags", Game::fieldtype_t::F_INT,
			[](Game::gentity_s* ent, [[maybe_unused]] int offset)
			{
				ent->flags = Game::Scr_GetInt(0);
			},
			[](Game::gentity_s* ent, [[maybe_unused]] int offset)
			{
				Game::Scr_AddInt(ent->flags);
			});
	}

	void ScriptExtension::AddClientFields()
	{
		ScriptExtension::AddClientField("clientflags", Game::fieldtype_t::F_INT,
			[](Game::gclient_s* pSelf, [[maybe_unused]] const Game::client_fields_s* pField)
			{
				pSelf->flags = Game::Scr_GetInt(0);
			},
			[](Game::gclient_s* pSelf, [[maybe_unused]] const Game::client_fields_s* pField)
			{
				Game::Scr_AddInt(pSelf->flags);
			});
	}

	ScriptExtension::ScriptExtension()
	{
		ScriptExtension::AddFunctions();
		ScriptExtension::AddMethods();
		ScriptExtension::AddEntityFields();
		ScriptExtension::AddClientFields();

		// Correct builtin function pointer
		Utils::Hook::Set<void(*)()>(0x79A90C, ScriptExtension::Scr_TableLookupIStringByRow);

		Utils::Hook(0x4EC721, ScriptExtension::GScr_AddFieldsForEntityStub, HOOK_CALL).install()->quick(); // GScr_AddFieldsForEntity

		Utils::Hook(0x41BED2, ScriptExtension::Scr_SetObjectFieldStub, HOOK_CALL).install()->quick(); // SetEntityFieldValue
		Utils::Hook(0x5FBF01, ScriptExtension::Scr_SetClientFieldStub, HOOK_CALL).install()->quick(); // Scr_SetObjectField
		Utils::Hook(0x4FF413, ScriptExtension::Scr_GetEntityFieldStub, HOOK_CALL).install()->quick(); // Scr_GetObjectField
	}
}
