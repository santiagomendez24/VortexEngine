// VortexEngine.cpp : Este archivo contiene la función "main". La ejecución del programa comienza y termina ahí.
//

#include <iostream>
#include <memory>
#include <print>
#include "../Core/include/LogQueue.h"
#include "../Network/include/LogServer.h"
#include "../Network/time/include/Time.h"
#include "../ThreadManager/include/ThreadManager.h"
#include "../Telemetry/include/Telemetry.h"

#include "../Core/src/LogQueue.cpp"
#include "../Network/src/LogServer.cpp"
#include "../Network/time/src/Time.cpp"
#include "../Telemetry/src/Telemetry.cpp"
#include "../ThreadManager/src/ThreadManager.cpp"

struct MainConfig
{
	size_t usable_ram = 128;
	Core::OverflowProfile profile = Core::OverflowProfile::DropAll;
	ThreadManager::ThreadProfile thread_profile = ThreadManager::ThreadProfile::TotalManual;
	ThreadManager::ThreadConfig thread_config = { thread_profile, 6, 2, 2 };
};

int main()
{
	MainConfig main_config;
	std::shared_ptr<Telemetry::Telemetry> telemetry = std::make_shared<Telemetry::Telemetry>();
	std::shared_ptr<Network::LogServer> LogServer = std::make_shared<Network::LogServer>(8080);

	ThreadManager::ThreadManager thread_manager(main_config.thread_config, telemetry, main_config.usable_ram, main_config.profile, LogServer);

	std::print("Enter para frenar\n");
	std::cin.get();
}

