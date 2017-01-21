#include "STDInclude.hpp"

namespace Components
{
	bool ServerList::SortAsc = true;
	int ServerList::SortKey = ServerList::Column::Ping;

	unsigned int ServerList::CurrentServer = 0;
	ServerList::Container ServerList::RefreshContainer;

	std::vector<ServerList::ServerInfo> ServerList::OnlineList;
	std::vector<ServerList::ServerInfo> ServerList::OfflineList;
	std::vector<ServerList::ServerInfo> ServerList::FavouriteList;

	std::vector<unsigned int> ServerList::VisibleList;

	std::vector<ServerList::ServerInfo>* ServerList::GetList()
	{
		if (ServerList::IsOnlineList())
		{
			return &ServerList::OnlineList;
		}
		else if (ServerList::IsOfflineList())
		{
			return &ServerList::OfflineList;
		}
		else if (ServerList::IsFavouriteList())
		{
			return &ServerList::FavouriteList;
		}

		return nullptr;
	}

	bool ServerList::IsFavouriteList()
	{
		return (Dvar::Var("ui_netSource").get<int>() == 2);
	}

	bool ServerList::IsOfflineList()
	{
		return (Dvar::Var("ui_netSource").get<int>() == 0);
	}

	bool ServerList::IsOnlineList()
	{
		return (Dvar::Var("ui_netSource").get<int>() == 1);
	}

	unsigned int ServerList::GetServerCount()
	{
		return ServerList::VisibleList.size();
	}

	const char* ServerList::GetServerText(unsigned int index, int column)
	{
		ServerList::ServerInfo* info = ServerList::GetServer(index);

		if (info)
		{
			return ServerList::GetServerInfoText(info, column);
		}

		return "";
	}

	const char* ServerList::GetServerInfoText(ServerList::ServerInfo* server, int column)
	{
		if (!server) return "";

		switch (column)
		{
			case Column::Password:
			{
				return (server->password ? "X" : "");
			}

			case Column::Matchtype:
			{
				return ((server->matchType == 1) ? "P" : "M");
			}

			case Column::Hostname:
			{
				return server->hostname.data();
			}

			case Column::Mapname:
			{
				if (server->svRunning)
				{
					return Game::UI_LocalizeMapName(server->mapname.data());
				}
				else
				{
					return Utils::String::VA("^3%s", Game::UI_LocalizeMapName(server->mapname.data()));
				}
			}

			case Column::Players:
			{
				return Utils::String::VA("%i (%i)", server->clients, server->maxClients);
			}

			case Column::Gametype:
			{
				return Game::UI_LocalizeGameType(server->gametype.data());
			}

			case Column::Mod:
			{
				if (server->mod != "")
				{
					return (server->mod.data() + 5);
				}

				return "";
			}

			case Column::Ping:
			{
				return Utils::String::VA("%i", server->ping);
			}

			default:
			{
				break;
			};
		}

		return "";
	}

	void ServerList::SelectServer(unsigned int index)
	{
		ServerList::CurrentServer = index;

		ServerList::ServerInfo* info = ServerList::GetCurrentServer();

		if (info)
		{
			Dvar::Var("ui_serverSelected").set(true);
			Dvar::Var("ui_serverSelectedMap").set(info->mapname);
			Dvar::Var("ui_serverSelectedGametype").set(info->gametype);
		}
		else
		{
			Dvar::Var("ui_serverSelected").set(false);
		}
	}

	void ServerList::UpdateVisibleList(UIScript::Token)
	{
		auto list = ServerList::GetList();
		if (!list) return;

		std::vector<ServerList::ServerInfo> tempList(*list);

		if (tempList.empty())
		{
			ServerList::Refresh(UIScript::Token());
		}
		else
		{
			list->clear();

			std::lock_guard<std::mutex> _(ServerList::RefreshContainer.mutex);

			ServerList::RefreshContainer.sendCount = 0;
			ServerList::RefreshContainer.sentCount = 0;

			for (auto server : tempList)
			{
				ServerList::InsertRequest(server.addr, false);
			}
		}
	}

	void ServerList::RefreshVisibleList(UIScript::Token)
	{
		Dvar::Var("ui_serverSelected").set(false);

		ServerList::VisibleList.clear();

		auto list = ServerList::GetList();
		if (!list) return;

		// Refresh entirely, if there is no entry in the list
		if (list->empty())
		{
			ServerList::Refresh(UIScript::Token());
			return;
		}

		bool ui_browserShowFull     = Dvar::Var("ui_browserShowFull").get<bool>();
		bool ui_browserShowEmpty    = Dvar::Var("ui_browserShowEmpty").get<bool>();
		int ui_browserShowHardcore  = Dvar::Var("ui_browserKillcam").get<int>();
		int ui_browserShowPassword  = Dvar::Var("ui_browserShowPassword").get<int>();
		int ui_browserMod           = Dvar::Var("ui_browserMod").get<int>();
		int ui_joinGametype         = Dvar::Var("ui_joinGametype").get<int>();

		for (unsigned int i = 0; i < list->size(); ++i)
		{
			ServerList::ServerInfo* info = &(*list)[i];

			// Filter full servers
			if (!ui_browserShowFull && info->clients >= info->maxClients) continue;

			// Filter empty servers
			if (!ui_browserShowEmpty && info->clients <= 0) continue;

			// Filter hardcore servers
			if ((ui_browserShowHardcore == 0 && info->hardcore) || (ui_browserShowHardcore == 1 && !info->hardcore)) continue;

			// Filter servers with password
			if ((ui_browserShowPassword == 0 && info->password) || (ui_browserShowPassword == 1 && !info->password)) continue;

			// Don't show modded servers
			if ((ui_browserMod == 0 && info->mod.size()) || (ui_browserMod == 1 && !info->mod.size())) continue;

			// Filter by gametype
			if (ui_joinGametype > 0 && (ui_joinGametype -1) < *Game::gameTypeCount  && Game::gameTypes[(ui_joinGametype - 1)].gameType != info->gametype) continue;

			ServerList::VisibleList.push_back(i);
		}

		ServerList::SortList();
	}

	void ServerList::Refresh(UIScript::Token)
	{
		Dvar::Var("ui_serverSelected").set(false);
		Localization::Set("MPUI_SERVERQUERIED", "Sent requests: 0/0");

// 		ServerList::OnlineList.clear();
// 		ServerList::OfflineList.clear();
// 		ServerList::FavouriteList.clear();

		auto list = ServerList::GetList();
		if (list) list->clear();

		ServerList::VisibleList.clear();
		ServerList::RefreshContainer.mutex.lock();
		ServerList::RefreshContainer.servers.clear();
		ServerList::RefreshContainer.sendCount = 0;
		ServerList::RefreshContainer.sentCount = 0;
		ServerList::RefreshContainer.mutex.unlock();

		if (ServerList::IsOfflineList())
		{
			Discovery::Perform();
		}
		else if (ServerList::IsOnlineList())
		{
#ifdef USE_LEGACY_SERVER_LIST
			ServerList::RefreshContainer.awatingList = true;
			ServerList::RefreshContainer.awaitTime = Game::Sys_Milliseconds();

			int masterPort = Dvar::Var("masterPort").get<int>();
			const char* masterServerName = Dvar::Var("masterServerName").get<const char*>();

			ServerList::RefreshContainer.host = Network::Address(Utils::String::VA("%s:%u", masterServerName, masterPort));

			Logger::Print("Sending serverlist request to master: %s:%u\n", masterServerName, masterPort);

			Network::SendCommand(ServerList::RefreshContainer.host, "getservers", Utils::String::VA("IW4 %i full empty", PROTOCOL));
			//Network::SendCommand(ServerList::RefreshContainer.Host, "getservers", "0 full empty");
#else
			Node::SyncNodeList();
#endif
		}
		else if (ServerList::IsFavouriteList())
		{
			ServerList::LoadFavourties();
		}
	}

	void ServerList::StoreFavourite(std::string server)
	{
		//json11::Json::parse()
		std::vector<std::string> servers;

		if (Utils::IO::FileExists("players/favourites.json"))
		{
			std::string data = Utils::IO::ReadFile("players/favourites.json");
			json11::Json object = json11::Json::parse(data, data);

			if (!object.is_array())
			{
				Logger::Print("Favourites storage file is invalid!\n");
				Game::MessageBox("Favourites storage file is invalid!", "Error");
				return;
			}

			auto storedServers = object.array_items();

			for (unsigned int i = 0; i < storedServers.size(); ++i)
			{
				if (!storedServers[i].is_string()) continue;
				if (storedServers[i].string_value() == server)
				{
					Game::MessageBox("Server already marked as favourite.", "Error");
					return;
				}

				servers.push_back(storedServers[i].string_value());
			}
		}

		servers.push_back(server);

		json11::Json data = json11::Json(servers);
		Utils::IO::WriteFile("players/favourites.json", data.dump());
		Game::MessageBox("Server added to favourites.", "Success");
	}

	void ServerList::RemoveFavourite(std::string server)
	{
		std::vector<std::string> servers;

		if (Utils::IO::FileExists("players/favourites.json"))
		{
			std::string data = Utils::IO::ReadFile("players/favourites.json");
			json11::Json object = json11::Json::parse(data, data);

			if (!object.is_array())
			{
				Logger::Print("Favourites storage file is invalid!\n");
				Game::MessageBox("Favourites storage file is invalid!", "Error");
				return;
			}

			for (auto& storedServer : object.array_items())
			{
				if (storedServer.is_string() && storedServer.string_value() != server)
				{
					servers.push_back(storedServer.string_value());
				}
			}
		}

		json11::Json data = json11::Json(servers);
		Utils::IO::WriteFile("players/favourites.json", data.dump());

		auto list = ServerList::GetList();
		if (list) list->clear();
		
		ServerList::RefreshVisibleList(UIScript::Token());
		
		Game::MessageBox("Server removed from favourites.", "Success");
	}

	void ServerList::LoadFavourties()
	{
		if (ServerList::IsFavouriteList() && Utils::IO::FileExists("players/favourites.json"))
		{
			auto list = ServerList::GetList();
			if (list) list->clear();

			std::string data = Utils::IO::ReadFile("players/favourites.json");
			json11::Json object = json11::Json::parse(data, data);

			if (!object.is_array())
			{
				Logger::Print("Favourites storage file is invalid!\n");
				Game::MessageBox("Favourites storage file is invalid!", "Error");
				return;
			}

			auto servers = object.array_items();

			for (unsigned int i = 0; i < servers.size(); ++i)
			{
				if(!servers[i].is_string()) continue;
				ServerList::InsertRequest(servers[i].string_value(), true);
			}
		}
	}

	void ServerList::InsertRequest(Network::Address address, bool acquireMutex)
	{
		if (acquireMutex) ServerList::RefreshContainer.mutex.lock();

		ServerList::Container::ServerContainer container;
		container.sent = false;
		container.target = address;

		bool alreadyInserted = false;
		for (auto &server : ServerList::RefreshContainer.servers)
		{
			if (server.target == container.target)
			{
				alreadyInserted = true;
				break;
			}
		}

		if (!alreadyInserted)
		{
			ServerList::RefreshContainer.servers.push_back(container);

			auto list = ServerList::GetList();
			if (list)
			{
				for (auto server : *list)
				{
					if (server.addr == container.target)
					{
						--ServerList::RefreshContainer.sendCount;
						--ServerList::RefreshContainer.sentCount;
						break;
					}
				}
			}

			++ServerList::RefreshContainer.sendCount;
		}

		if (acquireMutex) ServerList::RefreshContainer.mutex.unlock();
	}

	void ServerList::Insert(Network::Address address, Utils::InfoString info)
	{
		std::lock_guard<std::mutex> _(ServerList::RefreshContainer.mutex);

		for (auto i = ServerList::RefreshContainer.servers.begin(); i != ServerList::RefreshContainer.servers.end();)
		{
			// Our desired server
			if (i->target == address && i->sent)
			{
				// Challenge did not match
				if (i->challenge != info.get("challenge"))
				{
					// Shall we remove the server from the queue?
					// Better not, it might send a second response with the correct challenge.
					// This might happen when users refresh twice (or more often) in a short period of time
					break;
				}

				ServerInfo server;
				server.hostname = info.get("hostname");
				server.mapname = info.get("mapname");
				server.gametype = info.get("gametype");
				server.shortversion = info.get("shortversion");
				server.mod = info.get("fs_game");
				server.matchType = atoi(info.get("matchtype").data());
				server.clients = atoi(info.get("clients").data());
				server.securityLevel = atoi(info.get("securityLevel").data());
				server.maxClients = atoi(info.get("sv_maxclients").data());
				server.password = (atoi(info.get("isPrivate").data()) != 0);
				server.hardcore = (atoi(info.get("hc").data()) != 0);
				server.svRunning = (atoi(info.get("sv_running").data()) != 0);
				server.ping = (Game::Sys_Milliseconds() - i->sendTime);
				server.addr = address;

				// Remove server from queue
				i = ServerList::RefreshContainer.servers.erase(i);

				// Check if already inserted and remove
				auto list = ServerList::GetList();
				if (!list) return;

				unsigned int k = 0;
				for (auto j = list->begin(); j != list->end(); ++k)
				{
					if (j->addr == address)
					{
						j = list->erase(j);
					}
					else
					{
						++j;
					}
				}

				// Also remove from visible list
				for (auto j = ServerList::VisibleList.begin(); j != ServerList::VisibleList.end();)
				{
					if (*j == k)
					{
						j = ServerList::VisibleList.erase(j);
					}
					else
					{
						++j;
					}
				}

				if (info.get("gamename") == "IW4"
					&& server.matchType 
#ifndef DEBUG
					&& server.shortversion == SHORTVERSION
#endif
					)
				{
					auto lList = ServerList::GetList();

					if (lList)
					{
						lList->push_back(server);
						ServerList::RefreshVisibleList(UIScript::Token());
					}
				}

				break;
			}
			else
			{
				++i;
			}
		}
	}

	ServerList::ServerInfo* ServerList::GetCurrentServer()
	{
		return ServerList::GetServer(ServerList::CurrentServer);
	}

	void ServerList::SortList()
	{
		qsort(ServerList::VisibleList.data(), ServerList::VisibleList.size(), sizeof(int), [] (const void* first, const void* second)
		{
			const unsigned int server1 = *static_cast<const unsigned int*>(first);
			const unsigned int server2 = *static_cast<const unsigned int*>(second);

			ServerInfo* info1 = nullptr;
			ServerInfo* info2 = nullptr;

			auto list = ServerList::GetList();
			if (!list) return 0;

			if (list->size() > server1) info1 = &(*list)[server1];
			if (list->size() > server2) info2 = &(*list)[server2];

			if (!info1) return 1;
			if (!info2) return -1;

			// Numerical comparisons
			if (ServerList::SortKey == ServerList::Column::Ping)
			{
				return ((info1->ping - info2->ping) * (ServerList::SortAsc ? 1 : -1));
			}
			else if (ServerList::SortKey == ServerList::Column::Players)
			{
				return ((info1->clients - info2->clients) * (ServerList::SortAsc ? 1 : -1));
			}

			std::string text1 = Colors::Strip(ServerList::GetServerInfoText(info1, ServerList::SortKey));
			std::string text2 = Colors::Strip(ServerList::GetServerInfoText(info2, ServerList::SortKey));

			// ASCII-based comparison
			return (text1.compare(text2) * (ServerList::SortAsc ? 1 : -1));
		});
	}

	ServerList::ServerInfo* ServerList::GetServer(unsigned int index)
	{
		if (ServerList::VisibleList.size() > index)
		{
			auto list = ServerList::GetList();
			if (!list) return nullptr;

			if (list->size() > ServerList::VisibleList[index])
			{
				return &(*list)[ServerList::VisibleList[index]];
			}
		}

		return nullptr;
	}

	void ServerList::Frame()
	{
		// This is bad practice and might even cause undefined behaviour!
		if (!ServerList::RefreshContainer.mutex.try_lock()) return;

		if (ServerList::RefreshContainer.awatingList)
		{
			// Check if we haven't got a response within 10 seconds
			if (Game::Sys_Milliseconds() - ServerList::RefreshContainer.awaitTime > 5000)
			{
				ServerList::RefreshContainer.awatingList = false;

				Logger::Print("We haven't received a response from the master within %d seconds!\n", (Game::Sys_Milliseconds() - ServerList::RefreshContainer.awaitTime) / 1000);
			}
		}

		// Send requests to 10 servers each frame
		int SendServers = 10;
		
		for (unsigned int i = 0; i < ServerList::RefreshContainer.servers.size(); ++i)
		{
			ServerList::Container::ServerContainer* server = &ServerList::RefreshContainer.servers[i];
			if (server->sent) continue;

			// Found server we can send a request to
			server->sent = true;
			SendServers--;

			server->sendTime = Game::Sys_Milliseconds();
			server->challenge = Utils::Cryptography::Rand::GenerateChallenge();

			++ServerList::RefreshContainer.sentCount;

			Network::SendCommand(server->target, "getinfo", server->challenge);

			// Display in the menu, like in COD4
			Localization::Set("MPUI_SERVERQUERIED", Utils::String::VA("Sent requests: %d/%d", ServerList::RefreshContainer.sentCount, ServerList::RefreshContainer.sendCount));

			if (SendServers <= 0) break;
		}

		ServerList::RefreshContainer.mutex.unlock();
	}

	void ServerList::UpdateSource()
	{
		Dvar::Var netSource("ui_netSource");

		int source = netSource.get<int>();

		if (++source > netSource.get<Game::dvar_t*>()->max.i)
		{
			source = 0;
		}

		netSource.set(source);

		ServerList::RefreshVisibleList(UIScript::Token());
	}

	void ServerList::UpdateGameType()
	{
		Dvar::Var joinGametype("ui_joinGametype");

		int gametype = joinGametype.get<int>();

		if (++gametype > *Game::gameTypeCount)
		{
			gametype = 0;
		}

		joinGametype.set(gametype);

		ServerList::RefreshVisibleList(UIScript::Token());
	}

	ServerList::ServerList()
	{
		ServerList::OnlineList.clear();
		ServerList::VisibleList.clear();

		Dvar::OnInit([] ()
		{
			Dvar::Register<bool>("ui_serverSelected", false, Game::dvar_flag::DVAR_FLAG_NONE, "Whether a server has been selected in the serverlist");
			Dvar::Register<const char*>("ui_serverSelectedMap", "mp_afghan", Game::dvar_flag::DVAR_FLAG_NONE, "Map of the selected server");
		});

		Localization::Set("MPUI_SERVERQUERIED", "Sent requests: 0/0");

		Network::Handle("getServersResponse", [] (Network::Address address, std::string data)
		{
			if (ServerList::RefreshContainer.host != address) return; // Only parse from host we sent to

			ServerList::RefreshContainer.awatingList = false;

			std::lock_guard<std::mutex> _(ServerList::RefreshContainer.mutex);

			int offset = 0;
			int count = ServerList::RefreshContainer.servers.size();
			ServerList::MasterEntry* entry = nullptr;

			// Find first entry
			do 
			{
				entry = reinterpret_cast<ServerList::MasterEntry*>(const_cast<char*>(data.data()) + offset++);
			}
			while (!entry->HasSeparator() && !entry->IsEndToken());

			for (int i = 0; !entry[i].IsEndToken() && entry[i].HasSeparator(); ++i)
			{
				Network::Address serverAddr = address;
				serverAddr.setIP(entry[i].ip);
				serverAddr.setPort(ntohs(entry[i].port));
				serverAddr.setType(Game::NA_IP);

				ServerList::InsertRequest(serverAddr, false);
			}

			Logger::Print("Parsed %d servers from master\n", ServerList::RefreshContainer.servers.size() - count);
		});

		// Set default masterServerName + port and save it 
#ifdef USE_LEGACY_SERVER_LIST
		Utils::Hook::Set<char*>(0x60AD92, "localhost");
		Utils::Hook::Set<BYTE>(0x60AD90, Game::dvar_flag::DVAR_FLAG_SAVED); // masterServerName
		Utils::Hook::Set<BYTE>(0x60ADC6, Game::dvar_flag::DVAR_FLAG_SAVED); // masterPort
#endif

		// Add server list feeder
		UIFeeder::Add(2.0f, ServerList::GetServerCount, ServerList::GetServerText, ServerList::SelectServer);

		// Add required UIScripts
		UIScript::Add("UpdateFilter", ServerList::RefreshVisibleList);
		UIScript::Add("RefreshFilter", ServerList::UpdateVisibleList);

		UIScript::Add("RefreshServers", ServerList::Refresh);
		UIScript::Add("JoinServer", [] (UIScript::Token)
		{
			ServerList::ServerInfo* info = ServerList::GetServer(ServerList::CurrentServer);

			if (info)
			{
				Party::Connect(info->addr);
			}
		});
		UIScript::Add("ServerSort", [] (UIScript::Token token)
		{
			int key = token.get<int>();

			if (ServerList::SortKey == key)
			{
				ServerList::SortAsc = !ServerList::SortAsc;
			}
			else
			{
				ServerList::SortKey = key;
				ServerList::SortAsc = true;
			}

			Logger::Print("Sorting server list by token: %d\n", ServerList::SortKey);
			ServerList::SortList();
		});
		UIScript::Add("CreateListFavorite", [] (UIScript::Token)
		{
			ServerList::ServerInfo* info = ServerList::GetCurrentServer();

			if (info)
			{
				ServerList::StoreFavourite(info->addr.getString());
			}
		});
		UIScript::Add("CreateFavorite", [] (UIScript::Token)
		{
			ServerList::StoreFavourite(Dvar::Var("ui_favoriteAddress").get<std::string>());
		});
		UIScript::Add("CreateCurrentServerFavorite", [] (UIScript::Token)
		{
			if (Game::CL_IsCgameInitialized())
			{
				std::string addressText = Network::Address(*Game::connectedHost).getString();
				if (addressText != "0.0.0.0:0" && addressText != "loopback")
				{
					ServerList::StoreFavourite(addressText);
				}
			}
		});
		UIScript::Add("DeleteFavorite", [] (UIScript::Token)
		{
			ServerList::ServerInfo* info = ServerList::GetCurrentServer();

			if (info)
			{
				ServerList::RemoveFavourite(info->addr.getString());
			};
		});

		// Add required ownerDraws
		UIScript::AddOwnerDraw(220, ServerList::UpdateSource);
		UIScript::AddOwnerDraw(253, ServerList::UpdateGameType);

		// Add frame callback
		Renderer::OnFrame(ServerList::Frame);

		// This is placed here in case the anticheat has been disabled!
#if !defined(DEBUG) && !defined(DISABLE_ANTICHEAT)
		Renderer::OnFrame(AntiCheat::ReadIntegrityCheck);
#endif
	}

	ServerList::~ServerList()
	{
		ServerList::OnlineList.clear();
		ServerList::OfflineList.clear();
		ServerList::FavouriteList.clear();
		ServerList::VisibleList.clear();

		ServerList::RefreshContainer.mutex.lock();
		ServerList::RefreshContainer.awatingList = false;
		ServerList::RefreshContainer.servers.clear();
		ServerList::RefreshContainer.mutex.unlock();
	}
}
