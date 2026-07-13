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

//Edit this if you want to change the amount of RAM that log queue is gonna use
constexpr static size_t usable_ram = 30; // In MB

int main()
{
	Core::OverflowProfile profile = Core::OverflowProfile::Block;
	std::shared_ptr<Telemetry::Telemetry> telemetry = std::make_shared<Telemetry::Telemetry>();
	std::shared_ptr<Network::LogServer> LogServer = std::make_shared<Network::LogServer>(8080);

	ThreadManager::ThreadManager thread_manager({ThreadManager::ThreadProfile::TotalManual, 6, 2, 2}, telemetry, usable_ram, profile, LogServer);

	std::print("Enter para frenar\n");
	std::cin.get();
}

