#pragma once

#include "../Core/include/LogQueue.h"

struct MainConfig
{
	size_t usable_ram = 16;
	size_t log_size = 1;
	Core::OverflowProfile profile = Core::OverflowProfile::DropAll;
	size_t thread_num = 1;
	size_t max_slabs = 10;
	size_t slabs_size = log_size;
};