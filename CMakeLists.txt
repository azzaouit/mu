cmake_minimum_required(VERSION 3.10)

project(mu
    VERSION 0.0.0
    DESCRIPTION "Microsecond consensus"
    LANGUAGES C
)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O3 -Wall -Wextra -Werror")

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RELEASE)
endif()

if(CMAKE_BUILD_TYPE MATCHES DEBUG)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -fno-omit-frame-pointer")
endif()

find_library(IBVERBS_LIBRARY ibverbs REQUIRED)
find_library(RDMACM_LIBRARY rdmacm REQUIRED)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)
file(GLOB src_files "src/*.c")
add_executable(node ${src_files})
target_link_libraries(node ibverbs rdmacm)
