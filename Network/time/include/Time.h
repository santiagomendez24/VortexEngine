#pragma once
#include <memory>
#include <thread>
#include <atomic>

namespace Network
{
	namespace Tools
	{
		class Time
		{
		private:

			inline static std::atomic<uint64_t> unix_time{ 0 };

		public:

			static void update_time() noexcept;

			[[nodiscard]] inline static uint64_t GetTime() noexcept
			{
				return unix_time.load(std::memory_order_relaxed);
			}
		};
	}
}