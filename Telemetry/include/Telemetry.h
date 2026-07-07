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
		std::atomic<bool> active{ false };
		std::thread telemetry_thread;

	public:

		~Telemetry() noexcept { stop(); }

		void start() noexcept;
		void stop() noexcept;
		void RegisterPushed() noexcept;
		void RegisterEliminated() noexcept;
	};
}