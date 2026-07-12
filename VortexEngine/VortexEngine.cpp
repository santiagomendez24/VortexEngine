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
constexpr static size_t usable_ram = 50; // In MB

int main()
{
	ThreadManager::LogClasses logClasses;
	Core::OverflowProfile profile = Core::OverflowProfile::Block;

	auto telemetry = std::make_shared<Telemetry::Telemetry>();
	auto logQueue = std::make_shared<Core::LogQueue>(usable_ram, *telemetry, profile);
	auto logServer = std::make_shared<Network::LogServer>(8080);
	ThreadManager::ThreadManager thread_manager({ThreadManager::ThreadProfile::Automatic, 0, 0, 0});

	logClasses.logQueue = logQueue;
	logClasses.telemetry = telemetry;
	logClasses.logServer = logServer;

	thread_manager.start_threads(logClasses, usable_ram, profile);
	std::print("Enter para frenar");
	std::cin.get();
}

