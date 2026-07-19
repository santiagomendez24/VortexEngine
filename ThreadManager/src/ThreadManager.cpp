#include "../include/ThreadManager.h"
#include "../../Core/include/LogQueue.h"
#include <thread>
#include <memory>
#include <chrono>
#include <print>

namespace ThreadManager
{
	ThreadManager::ThreadManager(ThreadConfig user_config, std::shared_ptr<Telemetry::Telemetry> telemetry, size_t usable_ram, Core::OverflowProfile profile, std::shared_ptr<Network::LogServer> LogServer) noexcept : config(user_config)
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

		if (usable_ram < 32)
		{
			std::cerr << "[VORTEX ENGINE - WARN] Configuración de RAM invalida ("
				<< usable_ram << "). No puede ser menor a 32MB, colocando 32MB por seguridad"
				<< std::endl;
			usable_ram = 32;
		}

		LogClasses log_classes;
		log_classes.logServer = LogServer;
		log_classes.telemetry = telemetry;

		size_t thread_usable_ram = usable_ram > 15 ? usable_ram - 15 : 17;
		size_t raw_ram_per_thread = thread_usable_ram / config.threads_num;
		size_t thread_ram = std::bit_floor(raw_ram_per_thread);

		start_threads(log_classes, thread_ram, profile);
	}

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

	void ThreadManager::start_threads(const LogClasses& log_classes, size_t usable_ram, Core::OverflowProfile profile) noexcept
	{
		Network::Tools::Time::update_time();

		classes = log_classes;
		std::shared_ptr<Telemetry::Telemetry> local_telemetry = log_classes.telemetry;
		std::shared_ptr<Network::LogServer> local_logServer = log_classes.logServer;

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

		for (size_t i = 0; i < net_num; ++i)
		{
			owned_queues.push_back(std::make_unique<Core::LogQueue>(usable_ram, *local_telemetry, profile));
			all_queues.push_back(owned_queues.back().get());
		}

		time_thread = std::thread([this]()
		{
			while (is_on.load(std::memory_order_relaxed))
			{
				Network::Tools::Time::update_time();
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
		});

		telemetry_thread = std::thread([this, local_telemetry]()
		{
			while (is_on.load(std::memory_order_relaxed))
			{
				local_telemetry->update_telemetry();
				std::this_thread::sleep_for(std::chrono::seconds(2));
			}
		});

		for (size_t i = 0; i < consum_num; ++i)
		{
			Core::LogQueue* queue = all_queues[i];
			consumer_pool.emplace_back([queue]()
			{
				Core::LogEntry out_entry;
				bool KeepRunning = true;
				int spincounter = 0;

				while (KeepRunning)
				{
					if (queue->pop(out_entry))
					{
						//Salir
					}
					else
					{
						if (!queue->func_is_running())
						{
							KeepRunning = false;
							queue->head.notify_one();
						}
						else
						{
							if (spincounter < 100)
							{
								_mm_pause();
								spincounter++;
							}
							else if (spincounter < 500)
							{
								std::this_thread::yield();
								spincounter++;
							}
							else
							{
								size_t currenthead = queue->head.load(std::memory_order_acquire);

								if (!queue->pop(out_entry))
								{
									queue->head.wait(currenthead, std::memory_order_acquire);
									spincounter = 0;
								}
							}
						}
					}
				}

				while (queue->pop(out_entry))
				{
					//Salir
				}
			});
		}

		local_logServer->start(all_queues);
	}

	void ThreadManager::stop_threads(LogClasses log_classes) noexcept
	{
		log_classes.logServer->stop();

		for (auto* queue : all_queues)
		{
			if (queue)
			{
				queue->set_finished();
			}
		}

		is_on.store(false, std::memory_order_relaxed);

		for (auto& thread : consumer_pool)
		{
			if (thread.joinable())
			{
				thread.join();
			}
		}
		all_queues.clear();
		owned_queues.clear();

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