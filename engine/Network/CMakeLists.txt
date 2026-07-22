add_library(Network_Lib STATIC
    src/LogServer.cpp
)

target_include_directories(Network_Lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(Network_Lib PUBLIC Core_Lib asio::asio)