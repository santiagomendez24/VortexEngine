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

	public:

		void update_telemetry() noexcept;
		void RegisterPushed() noexcept;
		void RegisterEliminated() noexcept;
	};
}