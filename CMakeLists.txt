cmake_minimum_required(VERSION 3.14)

project(tasks-lens
    VERSION 0.1.0
    DESCRIPTION "Monitor de procesos a pantalla completa con control, sobre GuayabaTUIEngine"
    LANGUAGES CXX
)

if(NOT DEFINED CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 17)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
endif()

add_subdirectory(external/GuayabaTUIEngine)

add_executable(tasks-lens src/main.cpp)
target_link_libraries(tasks-lens PRIVATE GuayabaTUIEngine::GuayabaTUIEngine)
