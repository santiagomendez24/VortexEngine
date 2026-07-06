// VortexEngine.cpp : Este archivo contiene la función "main". La ejecución del programa comienza y termina ahí.
//

#include <iostream>
#include "../Core/include/LogQueue.h"
#include "../Network/include/LogServer.h"
#include "../Core/src/LogQueue.cpp"
#include "../Network/src/LogServer.cpp"

//Edit this if you want to change the amount of RAM that log queue is gonna use
constexpr static size_t usable_ram = 1; // In MB

int main()
{
	Core::LogQueue logQueue(usable_ram);
	Network::LogServer<Network::CheckLogEntry> logServer(8080, logQueue);
	logServer.start();
	std::cin.get();
	logServer.stop();
}

