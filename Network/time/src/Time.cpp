#include "../include/Time.h"
#include <chrono>

namespace Network
{
	namespace Tools
	{
		void Time::update_time() noexcept
		{
			auto ActualTime = std::chrono::system_clock::now();
			auto UnixDuration = std::chrono::duration_cast<std::chrono::seconds>(ActualTime.time_since_epoch());
			uint64_t CurrentTime = static_cast<uint64_t>(UnixDuration.count());

			unix_time.store(CurrentTime, std::memory_order_relaxed);
		}
	}
}