#include "../include/Time.h"
#include <chrono>

namespace Network
{
	void Time::start() noexcept
	{
		if (active.exchange(true)) return;

		time_thread = std::thread([]()
		{
			while (active.load(std::memory_order_relaxed))
			{
				auto ActualTime = std::chrono::system_clock::now();
				auto UnixDuration = std::chrono::duration_cast<std::chrono::seconds>(ActualTime.time_since_epoch());
				uint64_t CurrentTime = static_cast<uint64_t>(UnixDuration.count());

				unix_time.store(CurrentTime, std::memory_order_relaxed);

				std::this_thread::sleep_for(std::chrono::seconds(1));
			}
		});
	}

	[[nodiscard]] uint64_t Time::GetTime() noexcept
	{
		return unix_time.load(std::memory_order_relaxed);
	}

	void Time::stop() noexcept
	{
		if (!active.exchange(false)) return;

		if (time_thread.joinable())
		{
			time_thread.join();
		}
	}
}