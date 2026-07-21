#pragma once

#include "../ThreadManager/include/ThreadManager.h"
#include "../Core/include/LogQueue.h"

struct MainConfig
{
	size_t usable_ram = 16;
	size_t log_size = 1;
	Core::OverflowProfile profile = Core::OverflowProfile::DropAll;
	ThreadManager::ThreadConfig thread_config = { 4, 1, 1 };
	size_t max_slabs = 10;
	size_t slabs_size = log_size;
};