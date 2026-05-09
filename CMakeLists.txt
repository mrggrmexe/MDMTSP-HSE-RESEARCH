cmake_minimum_required(VERSION 3.20)

project(MDMTSP_HSE_RESEARCH VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

option(MDMTSP_BUILD_TESTS "Build tests" ON)

include(FetchContent)

find_package(nlohmann_json QUIET)

if(NOT nlohmann_json_FOUND)
    FetchContent_Declare(
        nlohmann_json
        URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    )
    FetchContent_MakeAvailable(nlohmann_json)
endif()

add_library(mdmtsp_project_options INTERFACE)
target_compile_features(mdmtsp_project_options INTERFACE cxx_std_20)

add_library(mdmtsp_project_warnings INTERFACE)

if(MSVC)
    target_compile_options(mdmtsp_project_warnings INTERFACE
        /W4
        /permissive-
    )
else()
    target_compile_options(mdmtsp_project_warnings INTERFACE
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Wconversion
        -Wsign-conversion
    )
endif()

add_library(mdmtsp_tsp STATIC
    src/tsp/lkh.cpp
    src/tsp/nearest_neighbour.cpp
    src/tsp/three_opt.cpp
    src/tsp/two_opt.cpp
)

target_include_directories(mdmtsp_tsp
    PUBLIC
        "${PROJECT_SOURCE_DIR}/src"
)

target_link_libraries(mdmtsp_tsp
    PUBLIC
        mdmtsp_project_options
    PRIVATE
        mdmtsp_project_warnings
)

add_library(mdmtsp_core STATIC
    src/mdmtsp/assignment.cpp
    src/mdmtsp/cheapest_insertion.cpp
    src/mdmtsp/cheapest_insertion_2opt.cpp
    src/mdmtsp/cheapest_insertion_tabu.cpp
    src/mdmtsp/cheapest_insertion_vnd.cpp
    src/mdmtsp/hgs_light_weight_v1.cpp
    src/mdmtsp/hgs_lightweight_v1.cpp
    src/mdmtsp/instance_io.cpp
    src/mdmtsp/interroute_local_search.cpp
    src/mdmtsp/lkh_mdmtsp.cpp
    src/mdmtsp/lkh_mdmtsp_v2.cpp
    src/mdmtsp/lkh_mdmtsp_v3.cpp
    src/mdmtsp/lkh_mdmtsp_v4_portfolio.cpp
    src/mdmtsp/lkh_mdmtsp_v5_portfolio.cpp
    src/mdmtsp/nearest_neighbour.cpp
    src/mdmtsp/nearest_neighbour_2opt.cpp
    src/mdmtsp/nearest_neighbour_2opt_v2.cpp
    src/mdmtsp/objective.cpp
    src/mdmtsp/random_insertion.cpp
    src/mdmtsp/simulated_annealing_v1.cpp
    src/mdmtsp/simulated_annealing_v2.cpp
    src/mdmtsp/solution.cpp
    src/mdmtsp/validator.cpp
)

target_include_directories(mdmtsp_core
    PUBLIC
        "${PROJECT_SOURCE_DIR}/src"
)

target_link_libraries(mdmtsp_core
    PUBLIC
        mdmtsp_project_options
        mdmtsp_tsp
        nlohmann_json::nlohmann_json
    PRIVATE
        mdmtsp_project_warnings
)

add_library(mdmtsp::tsp ALIAS mdmtsp_tsp)
add_library(mdmtsp::core ALIAS mdmtsp_core)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/app/main.cpp")
    add_executable(mdmtsp
        src/app/main.cpp
    )

    target_link_libraries(mdmtsp
    PRIVATE
        mdmtsp::core
        nlohmann_json::nlohmann_json
        mdmtsp_project_options
        mdmtsp_project_warnings
    )
endif()

if(MDMTSP_BUILD_TESTS)
    include(CTest)
    if(BUILD_TESTING AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
        add_subdirectory(tests)
    endif()
endif()