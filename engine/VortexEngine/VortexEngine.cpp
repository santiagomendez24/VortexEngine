// VortexEngine.cpp : Este archivo contiene la función "main". La ejecución del programa comienza y termina ahí.
//

#include <iostream>
#include <memory>
#include <print>
#include <json/json.hpp>
#include <fstream>

#include "../Core/include/LogQueue.h"
#include "../Network/include/LogServer.h"
#include "../Network/time/include/Time.h"
#include "../ThreadManager/include/ThreadManager.h"
#include "../Telemetry/include/Telemetry.h"
#include "../../shared/shared_memory_protocol.h"
#include "MainConfig.h"

#include "../Core/src/LogQueue.cpp"
#include "../Network/src/LogServer.cpp"
#include "../Network/time/src/Time.cpp"
#include "../Telemetry/src/Telemetry.cpp"
#include "../ThreadManager/src/ThreadManager.cpp"

using json = nlohmann::json;

MainConfig GetUserCofig(const std::string& filename)
{
	MainConfig config;

	std::ifstream file(filename);
	if (!file.is_open())
	{
		std::print(stderr, "[CONFIG] Can't open the file\n");
		return config;
	}

	json j;
	try
	{
		file >> j;
	}
	catch (const json::parse_error& e)
	{
		std::print(stderr, "Sintaxis error: {} \n Using default values \n", e.what());
		return config;
	}

	if (j.contains("usable_ram") && j["usable_ram"].is_number_unsigned())
		config.usable_ram = j["usable_ram"].get<size_t>();

	if (j.contains("log_size") && j["log_size"].is_number_unsigned())
		config.log_size = j["log_size"].get<size_t>();

	if (j.contains("thread_num") && j["thread_num"].is_number_unsigned())
		config.thread_num = j["thread_num"].get<size_t>();

	if (j.contains("max_slabs") && j["max_slabs"].is_number_unsigned())
		config.max_slabs = j["max_slabs"].get<size_t>();

	if (j.contains("profile") && j["profile"].is_number_integer())
	{
		int p = j["profile"].get<int>();
		if (p >= 0 && p <= 2)
		{
			config.profile = static_cast<Core::OverflowProfile>(p);
		}
		else
		{
			std::print(stderr, "[CONFIG] Profile out of range {} using default values\n", p);
		}
	}

	config.slabs_size = config.log_size;

	return config;
}

int main()
{
	MainConfig main_config = GetUserCofig("config.json");

	std::shared_ptr<Telemetry::Telemetry> telemetry = std::make_shared<Telemetry::Telemetry>();
	std::shared_ptr<Network::LogServer> LogServer = std::make_shared<Network::LogServer>(8080, main_config.log_size);

	ThreadManager::ThreadManager thread_manager(main_config, telemetry, LogServer);

	std::print("Enter para frenar\n");
	std::cin.get();
}

