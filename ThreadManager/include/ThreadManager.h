#pragma once

#include <atomic>
#include <vector>
#include <optional>
#include <asio.hpp>
#include <new>

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
		std::shared_ptr<Telemetry::Telemetry> telemetry;
		std::shared_ptr<Core::LogQueue> logQueue;
		std::shared_ptr<Network::LogServer<Network::CheckLogEntry>> logServer;
	};

	class ThreadManager
	{
	private:

		std::vector<std::thread> network_pool;
		std::vector<std::thread> consumer_pool;

		std::thread time_thread;
		std::thread telemetry_thread;

		std::atomic<bool> is_on{ false };

		ThreadConfig config;

		[[nodiscard]] size_t CalculateAutomaticThread(size_t TotalHardware) noexcept;

		void ValidateTotalManual();

	public:

		ThreadManager(ThreadConfig user_config) noexcept : config(user_config) 
		{ 
			size_t calc = CalculateThreads(config);
			if (calc == 0)
			{
				std::cerr << "[VORTEX ENGINE - WARN] Configuracion de hilos invalida ("
					<< config.threads_num << "). Fallback a perfil Automatico aplicado."
					<< std::endl;
				config.thread_profile = ThreadProfile::Automatic;
				config.threads_num = CalculateThreads(config);
			}
			else
			{
				config.threads_num = calc;
			}
		}

		size_t CalculateThreads(ThreadConfig config) noexcept;

		void start_threads(const LogClasses& log_classes) noexcept;

		void stop_threads(LogClasses log_classes) noexcept;
	};
}