#pragma once
#include <memory>
#include <thread>
#include <atomic>

namespace Network
{
	class Time
	{
	private:

		inline static std::atomic<uint64_t> unix_time{ 0 };

	public:

		static void update_time() noexcept;

		[[nodiscard]] static uint64_t GetTime() noexcept;
	};
}