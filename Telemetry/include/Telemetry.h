#pragma once
#include <memory>
#include <atomic>
#include <thread>

namespace Telemetry
{
	class Telemetry : public std::enable_shared_from_this<Telemetry>
	{
	private:

		std::atomic<uint64_t> pushed_logs{ 0 };
		std::atomic<uint64_t> eliminated_logs{ 0 };

		uint64_t last_pushed = 0;
		uint64_t last_popped = 0;

	public:

		void update_telemetry() noexcept;
		void RegisterPushed() noexcept;
		void RegisterEliminated() noexcept;
		void GetVelocity(uint64_t pushed, uint64_t popped) noexcept;
	};
}