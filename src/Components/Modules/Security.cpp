#include <STDInclude.hpp>

namespace Components
{
	int Security::MsgReadBitsCompressCheckSV(const char* from, char* to, int size)
	{
		static char buffer[0x8000];

		if (size > 0x800) return 0;
		size = Game::MSG_ReadBitsCompress(from, buffer, size);

		if (size > 0x800) return 0;
		std::memcpy(to, buffer, size);

		return size;
	}

	int Security::MsgReadBitsCompressCheckCL(const char* from, char* to, int size)
	{
		static char buffer[0x100000];

		if (size > 0x20000) return 0;
		size = Game::MSG_ReadBitsCompress(from, buffer, size);

		if (size > 0x20000) return 0;
		std::memcpy(to, buffer, size);

		return size;
	}

	int Security::SVCanReplaceServerCommand(Game::client_t* /*client*/, const char* /*cmd*/)
	{
		// This is a fix copied from V2. As I don't have time to investigate, let's simply trust them
		return -1;
	}

	long Security::AtolAdjustPlayerLimit(const char* string)
	{
		return std::min<long>(std::atol(string), 18);
	}

	void Security::SelectStringTableEntryInDvarStub()
	{
		Command::ClientParams params;

		if (params.size() >= 4)
		{
			const auto* dvarName = params[3];
			const auto* dvar = Game::Dvar_FindVar(dvarName);

			if (Command::Find(dvarName) ||
				(dvar != nullptr && dvar->flags & (Game::DVAR_WRITEPROTECTED | Game::DVAR_CHEAT | Game::DVAR_READONLY)))
			{
				Logger::Print(0, "CL_SelectStringTableEntryInDvar_f: illegal parameter\n");
				return;
			}
		}

		Game::CL_SelectStringTableEntryInDvar_f();
	}

	__declspec(naked) int Security::G_GetClientScore()
	{
		__asm
		{
			mov eax, [esp + 4] // index
			mov ecx, ds:1A831A8h // level: &g_clients

			test ecx, ecx
			jz invalid_ptr

			imul eax, 366Ch
			mov eax, [eax + ecx + 3134h]
			ret

		invalid_ptr:
			xor eax, eax
			ret
		}
	}

	void Security::G_LogPrintfStub(const char* fmt)
	{
		Game::G_LogPrintf("%s", fmt);
	}

	Security::Security()
	{
		// Exploit fixes
		Utils::Hook(0x414D92, MsgReadBitsCompressCheckSV, HOOK_CALL).install()->quick(); // SV_ExecuteClientCommands
		Utils::Hook(0x4A9F56, MsgReadBitsCompressCheckCL, HOOK_CALL).install()->quick(); // CL_ParseServerMessage
		Utils::Hook(0x407376, SVCanReplaceServerCommand, HOOK_CALL).install()->quick(); // SV_CanReplaceServerCommand

		Utils::Hook::Set<BYTE>(0x412370, 0xC3); // SV_SteamAuthClient
		Utils::Hook::Set<BYTE>(0x5A8C70, 0xC3); // CL_HandleRelayPacket

		Utils::Hook::Nop(0x41698E, 5); // Disable Svcmd_EntityList_f

		// Patch selectStringTableEntryInDvar
		Utils::Hook::Set<void(*)()>(0x405959, Security::SelectStringTableEntryInDvarStub);

		// Patch G_GetClientScore for uninitialized game
		Utils::Hook(0x469AC0, G_GetClientScore, HOOK_JUMP).install()->quick();

		// Requests can be malicious
		Utils::Hook(0x5B67ED, AtolAdjustPlayerLimit, HOOK_CALL).install()->quick(); // PartyHost_HandleJoinPartyRequest

		// Patch unsecure call to G_LogPrint inside GScr_LogPrint
		// This function is unsafe because IW devs forgot to G_LogPrintf("%s", fmt)
		Utils::Hook(0x5F70B5, G_LogPrintfStub, HOOK_CALL).install()->quick();
	}
}
