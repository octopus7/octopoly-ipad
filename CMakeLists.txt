cmake_minimum_required(VERSION 3.20)
project(OctoPolyCore VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(octopoly_core STATIC
    core/src/mesh.cpp
)
target_include_directories(octopoly_core PUBLIC core/include)

add_executable(octopoly_core_tests
    tests/test_mesh.cpp
)
target_link_libraries(octopoly_core_tests PRIVATE octopoly_core)

if(MSVC)
    target_compile_options(octopoly_core PRIVATE /W4 /WX /permissive-)
    target_compile_options(octopoly_core_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(octopoly_core PRIVATE -Wall -Wextra -Wpedantic -Werror)
    target_compile_options(octopoly_core_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

enable_testing()
add_test(NAME octopoly_core_tests COMMAND octopoly_core_tests)
