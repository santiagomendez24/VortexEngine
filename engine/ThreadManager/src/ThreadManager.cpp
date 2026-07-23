#include "../include/ThreadManager.h"
#include "../../Core/include/LogQueue.h"
#include "../../VortexEngine/MainConfig.h"
#include <thread>
#include <memory>
#include <chrono>
#include <print>

namespace ThreadManager
{
	ThreadManager::ThreadManager(MainConfig mainconfig, std::shared_ptr<Telemetry::Telemetry> telemetry, std::shared_ptr<Network::LogServer> LogServer) noexcept
	{
		size_t thread = mainconfig.thread_num;
		if (thread == 0)
		{
			std::cerr << "[VORTEX ENGINE - WARN] Configuracion de hilos invalida ("
				<< mainconfig.thread_num << "). Minimo un hilo, usando un hilo." << std::endl;
			mainconfig.thread_num = 1;
		}

		if (mainconfig.usable_ram < 16)
		{
			std::cerr << "[VORTEX ENGINE - WARN] Configuración de RAM invalida ("
				<< mainconfig.usable_ram << "). No puede ser menor a 16MB, colocando 16MB por seguridad" << std::endl;
			mainconfig.usable_ram = 16;
		}

		LogClasses log_classes;
		log_classes.logServer = LogServer;
		log_classes.telemetry = telemetry;

		size_t thread_ram = std::bit_floor(mainconfig.usable_ram);
		mainconfig.usable_ram = thread_ram;

		start_threads(log_classes, mainconfig);
	}

	void ThreadManager::start_threads(const LogClasses& log_classes, MainConfig mainconfig) noexcept
	{
		Network::Tools::Time::update_time();

		classes = log_classes;
		std::shared_ptr<Telemetry::Telemetry> local_telemetry = log_classes.telemetry;
		std::shared_ptr<Network::LogServer> local_logServer = log_classes.logServer;

		is_on.store(true, std::memory_order_relaxed);

		size_t thread_n = mainconfig.thread_num;

		for (size_t i = 0; i < thread_n; ++i)
		{
			owned_queues.push_back(std::make_unique<Core::LogQueue>(mainconfig, *local_telemetry, static_cast<uint32_t>(i)));
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

		for (size_t i = 0; i < thread_n; ++i)
		{
			Core::LogQueue* queue = all_queues[i];
			thread_pool.emplace_back([this, queue]()
			{
				Core::LogEntry out_entry;
				bool KeepRunning = true;
				int spincounter = 0;

				while (KeepRunning)
				{
					if (queue->pop(out_entry))
					{
						SendToPy(out_entry, queue->slab_pool.get());
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
					SendToPy(out_entry, queue->slab_pool.get());
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

		for (auto& thread : thread_pool)
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

	void ThreadManager::SendToPy(Core::LogEntry& out_entry, Network::SlabPool* slab_pool) noexcept
	{
		auto* shm_base = static_cast<char*>(slab_pool->shared_memory_base_ptr);
		if (!shm_base) return;

		auto* header = reinterpret_cast<SharedMemoryControl*>(shm_base);

		header->write_offset.fetch_add(out_entry.message_lenght, std::memory_order_release);
	}
}