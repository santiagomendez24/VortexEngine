#pragma once
#include <memory>
#include <thread>
#include <atomic>

namespace Network
{
	class Time : public std::enable_shared_from_this<Time>
	{
	private:

		inline static std::atomic<uint64_t> unix_time{ 0 };
		inline static std::atomic<bool> active{ false };
		inline static std::thread time_thread;

	public:

		Time() noexcept = default;
		~Time() noexcept { stop(); }

		static void start() noexcept;
		static void stop() noexcept;

		[[nodiscard]] static uint64_t GetTime() noexcept;
	};
}