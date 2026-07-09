#include "../include/Telemetry.h"
#include <chrono>

namespace Telemetry
{
	void Telemetry::update_telemetry() noexcept
	{
		uint64_t pushed = pushed_logs.load(std::memory_order_relaxed);
		uint64_t eliminated = eliminated_logs.load(std::memory_order_relaxed);
		uint64_t queue_bytes = current_queue_bytes.load(std::memory_order_relaxed);

		//Tirar reporte
	}

	void Telemetry::RegisterPushed() noexcept
	{
		pushed_logs.fetch_add(1, std::memory_order_relaxed);
	}

	void Telemetry::RegisterEliminated() noexcept
	{
		eliminated_logs.fetch_add(1, std::memory_order_relaxed);
	}

	void Telemetry::UpdateQueueBytes(size_t CurrentBytes) noexcept
	{
		current_queue_bytes.exchange(CurrentBytes, std::memory_order_relaxed);
	}
}