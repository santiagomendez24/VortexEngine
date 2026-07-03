add_library(Network_Lib STATIC
    src/LogServer.cpp
)

target_include_directories(Network_Lib PUBLIC include)
target_link_libraries(Network_Lib PUBLIC Core_Lib asio::asio)