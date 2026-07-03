add_library(Core_Lib STATIC
    src/LogQueue.cpp
)

target_include_directories(Core_Lib PUBLIC include)