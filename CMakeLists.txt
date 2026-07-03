cmake_minimum_required(VERSION 3.14)
project(VortexEngine CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

FetchContent_Declare(
  asio
  GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
  GIT_TAG        asio-1-30-2
)

FetchContent_MakeAvailable(asio)

# Target virtual para la biblioteca Header-Only de Asio
add_library(asio_lib INTERFACE)
target_include_directories(asio_lib INTERFACE ${asio_SOURCE_DIR}/include)
add_library(asio::asio ALIAS asio_lib)

# Módulos del motor
add_subdirectory(Core)
add_subdirectory(Network)
add_subdirectory(VortexEngine)