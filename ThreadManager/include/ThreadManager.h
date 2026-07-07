#pragma once

#include <atomic>
#include <vector>
#include <optional>
#include <asio.hpp>

namespace ThreadManager
{
	enum class ThreadProfile
	{
		Automatic,
		FastManual,
		TotalManual,
		NativeOS
	};

	struct ThreadConfig
	{
		ThreadProfile thread_profile = ThreadProfile::Automatic;
		size_t threads_num = 0;
		size_t network_num = 0;
		size_t consumer_num = 0;
	};

	struct LogClasses
	{
		Telemetry::Telemetry* telemetry;
		Core::LogQueue* logQueue;
		Network::Time* time;
		Network::LogServer<Network::CheckLogEntry>* logServer;
	};

	class ThreadManager
	{
	private:

		std::vector<std::thread> network_pool;
		std::vector<std::thread> consumer_pool;

		std::thread time_thread;
		std::thread telemetry_thread;

		std::optional<asio::executor_work_guard<asio::io_context::executor_type>> network_work_guard;

		std::atomic<bool> is_on{ false };

		ThreadConfig config;

		[[nodiscard]] size_t CalculateAutomaticThread(size_t TotalHardware) noexcept;

	public:

		ThreadManager(ThreadConfig user_config) noexcept : config(user_config) { config.threads_num = CalculateThreads(config); }

		size_t CalculateThreads(ThreadConfig config) noexcept;

		void start_threads(LogClasses log_classes) noexcept;

		void stop_threads(LogClasses log_classes) noexcept;
	};
}