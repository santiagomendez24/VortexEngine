// VortexEngine.cpp : Este archivo contiene la función "main". La ejecución del programa comienza y termina ahí.
//

#include <iostream>
#include "../Core/include/LogQueue.h"
#include "../Network/include/LogServer.h"
#include "../Core/src/LogQueue.cpp"
#include "../Network/time/include/Time.h"
#include "../Network/time/src/Time.cpp"
#include "../Network/src/LogServer.cpp"
#include "../ThreadManager/include/ThreadManager.h"
#include "../ThreadManager/src/ThreadManager.cpp"
#include "../Telemetry/include/Telemetry.h"
#include "../Telemetry/src/Telemetry.cpp"

//Edit this if you want to change the amount of RAM that log queue is gonna use
constexpr static size_t usable_ram = 1; // In MB

int main()
{
	ThreadManager::LogClasses logClasses;
	Telemetry::Telemetry telemetry;
	Core::LogQueue logQueue(usable_ram, telemetry);
	Network::Time time;
	Network::LogServer<Network::CheckLogEntry> logServer(8080, logQueue);
	ThreadManager::ThreadManager thread_manager({ThreadManager::ThreadProfile::Automatic, 0, 0, 0});

	logClasses.logQueue = &logQueue;
	logClasses.telemetry = &telemetry;
	logClasses.time = &time;
	logClasses.logServer = &logServer;

	thread_manager.start_threads(logClasses);
	std::cin.get();
	thread_manager.stop_threads(logClasses);
}

