#pragma once

#include <atomic>
#include <vector>
#include <optional>
#include <asio.hpp>
#include <new>

namespace ThreadManager
{
	struct ThreadConfig
	{
		size_t threads_num = 0;
		size_t network_num = 0;
		size_t consumer_num = 0;
	};

	struct LogClasses
	{
		std::shared_ptr<Telemetry::Telemetry> telemetry;
		std::shared_ptr<Network::LogServer> logServer;
	};

	class ThreadManager
	{
	private:

		ThreadConfig config;

		std::vector<std::unique_ptr<Core::LogQueue>> owned_queues;
		std::vector<Core::LogQueue*> all_queues;

		std::vector<std::thread> network_pool;
		std::vector<std::thread> consumer_pool;

		std::thread time_thread;
		std::thread telemetry_thread;

		std::atomic<bool> is_on{ false };

		LogClasses classes;

	public:

		ThreadManager(MainConfig mainconfig, std::shared_ptr<Telemetry::Telemetry> telemetry, std::shared_ptr<Network::LogServer> LogServer) noexcept;

		~ThreadManager() noexcept { stop_threads(classes); }

		size_t CalculateThreads(ThreadConfig config) noexcept;

		void start_threads(const LogClasses& log_classes, MainConfig mainconfig) noexcept;

		void stop_threads(LogClasses log_classes) noexcept;
	};
}