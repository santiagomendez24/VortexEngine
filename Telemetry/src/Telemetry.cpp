#include "../include/Telemetry.h"
#include <chrono>
#include <print>

namespace Telemetry
{
	void Telemetry::update_telemetry() noexcept
	{
		uint64_t pushed = pushed_logs.load(std::memory_order_relaxed);
		uint64_t eliminated = eliminated_logs.load(std::memory_order_relaxed);

		std::print("Pushed: {}, Popped: {} \n", pushed, eliminated);

		GetVelocity(pushed, eliminated);
	}

	void Telemetry::RegisterPushed() noexcept
	{
		pushed_logs.fetch_add(1, std::memory_order_relaxed);
	}

	void Telemetry::RegisterEliminated() noexcept
	{
		eliminated_logs.fetch_add(1, std::memory_order_relaxed);
	}

	void Telemetry::GetVelocity(uint64_t pushed, uint64_t popped) noexcept
	{
		uint64_t delta_push = pushed - last_pushed;
		uint64_t delta_popped = popped - last_popped;

		double push_per_sec = static_cast<double>(delta_push) / 2.0;
		double pop_per_sec = static_cast<double>(delta_popped) / 2.0;

		std::print("Push por segundo: {} logs/s, Pop por segundo: {} logs/s \n", push_per_sec, pop_per_sec);
		
		last_pushed = pushed;
		last_popped = popped;
	}
}