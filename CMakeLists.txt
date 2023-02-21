cmake_minimum_required(VERSION 3.5)
set(project_name dns_server)
project(${project_name} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
file(GLOB_RECURSE HEADERS ${SRC_DIR}/src/*.hpp)
file(GLOB_RECURSE SOURCES ${SRC_DIR}/src/*.cpp)
add_executable(${project_name} ${SOURCES} ${HEADERS} ${QT_CUSTOM})

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
target_link_libraries(${project_name} PRIVATE Threads::Threads)
