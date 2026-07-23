#pragma once

#include <atomic>
#include <vector>
#include <optional>
#include <asio.hpp>
#include <new>

namespace ThreadManager
{
	struct LogClasses
	{
		std::shared_ptr<Telemetry::Telemetry> telemetry;
		std::shared_ptr<Network::LogServer> logServer;
	};

	class ThreadManager
	{
	private:

		std::vector<std::unique_ptr<Core::LogQueue>> owned_queues;
		std::vector<Core::LogQueue*> all_queues;

		std::vector<std::thread> thread_pool;

		std::thread time_thread;
		std::thread telemetry_thread;

		std::atomic<bool> is_on{ false };

		LogClasses classes;

	public:

		ThreadManager(MainConfig mainconfig, std::shared_ptr<Telemetry::Telemetry> telemetry, std::shared_ptr<Network::LogServer> LogServer) noexcept;

		~ThreadManager() noexcept { stop_threads(classes); }

		void start_threads(const LogClasses& log_classes, MainConfig mainconfig) noexcept;

		void stop_threads(LogClasses log_classes) noexcept;

	private:

		void SendToPy(Core::LogEntry& out_entry, Network::SlabPool* slab_pool) noexcept;
	};
}