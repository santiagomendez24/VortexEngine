#include "../include/ThreadManager.h"
#include <thread>
#include <memory>
#include <chrono>

namespace ThreadManager
{
	size_t ThreadManager::CalculateThreads(ThreadConfig config) noexcept
	{
		const size_t total_thread = std::thread::hardware_concurrency();

		if (config.thread_profile == ThreadProfile::NativeOS)
		{
			return total_thread;
		}

		if (config.thread_profile == ThreadProfile::Automatic)
		{
			return CalculateAutomaticThread(total_thread);
		}

		if (config.thread_profile == ThreadProfile::FastManual)
		{
			if (config.threads_num <= 0 || config.threads_num >= total_thread - 2) return 0;

			return config.threads_num;
		}

		if (config.thread_profile == ThreadProfile::TotalManual)
		{
			size_t net = config.network_num;
			size_t pop = config.consumer_num;
			size_t sum = net + pop;

			if (sum >= total_thread - 2 || sum <= 0 || config.threads_num <= 0 || config.threads_num >= total_thread - 2)
			{
				return 0;
			}

			return config.threads_num;
		}

		return total_thread;
	}

	[[nodiscard]] size_t ThreadManager::CalculateAutomaticThread(size_t TotalHardware) noexcept
	{
		if (TotalHardware <= 4)
		{
			return TotalHardware;
		}

		if (TotalHardware <= 16)
		{
			return TotalHardware - 2;
		}

		size_t safe_threads = (TotalHardware / 16) * 2;
		size_t thread_calc = TotalHardware - safe_threads;

		const size_t max_cap = 12;

		if (thread_calc > max_cap)
		{
			return max_cap;
		}

		return thread_calc;
	}

	void ThreadManager::start_threads(const LogClasses& log_classes) noexcept
	{
		Network::Time::update_time();

		auto local_telemetry = log_classes.telemetry;
		auto local_logQueue = log_classes.logQueue;
		auto local_logServer = log_classes.logServer;

		is_on.store(true, std::memory_order_relaxed);

		size_t thread_n = config.threads_num;
		size_t net_num = 0;
		size_t consum_num = 0;

		if (config.thread_profile == ThreadProfile::TotalManual)
		{
			net_num = config.network_num;
			consum_num = config.consumer_num;
		}
		else
		{
			net_num = (config.network_num == 0) ? thread_n / 2 : config.network_num;

			consum_num = thread_n - net_num;

			if (net_num == 0) net_num = 1;
			if (consum_num == 0) consum_num = 1;
		}

		time_thread = std::thread([this]()
		{
			while (is_on.load(std::memory_order_relaxed))
			{
				Network::Time::update_time();
				std::this_thread::sleep_for(std::chrono::seconds(2));
			}
		});

		telemetry_thread = std::thread([this, local_telemetry]()
		{
			while (is_on.load(std::memory_order_relaxed))
			{
				local_telemetry->update_telemetry();
				std::this_thread::sleep_for(std::chrono::seconds(1));
			}
		});

		local_logServer->start_accept();
		for (size_t i = 0; i < net_num; ++i)
		{
			network_pool.emplace_back([local_logServer]()
			{
				local_logServer->start();
			});
		}

		for (size_t i = 0; i < consum_num; ++i)
		{
			consumer_pool.emplace_back([local_logQueue]()
			{
				Core::LogEntry out_entry;
				while (local_logQueue->pop(out_entry))
				{
					//aqui lo saco
				}
			});
		}
	}

	void ThreadManager::stop_threads(LogClasses log_classes) noexcept
	{
		log_classes.logServer->stop();
		log_classes.logQueue->set_finished();

		is_on.store(false, std::memory_order_relaxed);

		for (auto& thread : network_pool)
		{
			if (thread.joinable())
			{
				thread.join();
			}
		}

		for (auto& thread : consumer_pool)
		{
			if (thread.joinable())
			{
				thread.join();
			}
		}

		if (time_thread.joinable())
		{
			time_thread.join();
		}

		if (telemetry_thread.joinable())
		{
			telemetry_thread.join();
		}
	}
}