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
			if (config.threads_num <= 0 || config.threads_num > total_thread) return total_thread;

			return config.threads_num;
		}

		if (config.thread_profile == ThreadProfile::TotalManual)
		{
			// Configuracion total, la programo ahorita
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

	void ThreadManager::start_threads(LogClasses log_classes) noexcept
	{
		is_on.store(true, std::memory_order_relaxed);

		size_t thread_n = config.threads_num;

		if (thread_n >= 2) thread_n -= 2;

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

		time_thread = std::thread([this, log_classes]()
		{
			while (is_on.load(std::memory_order_relaxed))
			{
				std::this_thread::sleep_for(std::chrono::seconds(2));
				log_classes.time->update_time();
			}
		});

		telemetry_thread = std::thread([this, log_classes]()
		{
			while (is_on.load(std::memory_order_relaxed))
			{
				log_classes.telemetry->update_telemetry();
				std::this_thread::sleep_for(std::chrono::seconds(1));
			}
		});

		for (size_t i = 0; i < net_num; ++i)
		{
			network_pool.emplace_back([log_classes]()
			{
				log_classes.logServer->start();
			});
		}

		for (size_t i = 0; i < consum_num; ++i)
		{
			consumer_pool.emplace_back([log_classes]()
			{
				Core::LogEntry out_entry;
				while (log_classes.logQueue->pop(out_entry))
				{
					//aqui lo saco
				}
			});
		}
	}

	void ThreadManager::stop_threads(LogClasses log_classes) noexcept
	{
		network_work_guard.reset();
		log_classes.logServer->stop();
		is_on.store(false, std::memory_order_relaxed);

		for (auto& thread : network_pool)
		{
			if (thread.joinable())
			{
				thread.join();
			}
		}

		log_classes.logQueue->set_finished();

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