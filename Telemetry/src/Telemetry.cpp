#include "../include/Telemetry.h"
#include <chrono>

namespace Telemetry
{
	void Telemetry::start() noexcept
	{
		if (active.exchange(true)) return;

		telemetry_thread = std::thread([this]()
		{
			while (active.load(std::memory_order_relaxed()))
			{
				std::this_thread::sleep_for(std::chrono::seconds(2));

				uint64_t pushed = pushed_logs.load(std::memory_order_relaxed);
				uint64_t eliminated = eliminated_logs.load(std::memory_order_relaxed);

				//Tirar reporte
			}
		});
	}

	void Telemetry::stop() noexcept
	{
		if (!active.exchange(false)) return;

		if (telemetry_thread.joinable())
		{
			telemetry_thread.join();
		}
	}

	void Telemetry::RegisterPushed() noexcept
	{
		pushed_logs.fetch_add(1, std::memory_order_relaxed);
	}

	void Telemetry::RegisterEliminated() noexcept
	{
		eliminated_logs.fetch_add(1, std::memory_order_relaxed);
	}
}