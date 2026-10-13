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

# --- Platform backend selection -----------------------------------------------
# Mirrors the engine: glob every source, then drop the one backend that belongs
# to another OS. The dropped .cpp is also #ifdef-guarded, so this is hygiene.
if(WIN32)
elseif(UNIX)
else()
    message(FATAL_ERROR "tasks-lens: unsupported platform. Only Windows and Linux.")
endif()

file(GLOB_RECURSE TASKS_LENS_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

if(WIN32)
    list(REMOVE_ITEM TASKS_LENS_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/src/Data/platform/linux/linux_process_source.cpp"
    )
else()
    list(REMOVE_ITEM TASKS_LENS_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/src/Data/platform/windows/windows_process_source.cpp"
    )
endif()

find_package(Threads REQUIRED)

add_executable(tasks-lens ${TASKS_LENS_SOURCES})
target_include_directories(tasks-lens PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(tasks-lens PRIVATE
    GuayabaTUIEngine::GuayabaTUIEngine
    Threads::Threads
)

if(WIN32)
    target_link_libraries(tasks-lens PRIVATE psapi)
endif()
