cmake_minimum_required(VERSION 3.20)
project(OctoPolyCore VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(octopoly_core STATIC
    core/src/mesh.cpp
    core/src/project_codec.cpp
    core/src/glb_codec.cpp
)
target_include_directories(octopoly_core PUBLIC core/include)

add_executable(octopoly_core_tests
    tests/test_mesh.cpp
)
target_link_libraries(octopoly_core_tests PRIVATE octopoly_core)

add_executable(project_codec_tests
    tests/test_project_codec.cpp
)
target_link_libraries(project_codec_tests PRIVATE octopoly_core)

add_executable(mesh_allocation_fault_tests
    tests/test_mesh_allocation_faults.cpp
)
target_link_libraries(mesh_allocation_fault_tests PRIVATE octopoly_core)

add_executable(glb_codec_tests
    tests/test_glb_codec.cpp
)
target_link_libraries(glb_codec_tests PRIVATE octopoly_core)

if(MSVC)
    target_compile_options(octopoly_core PRIVATE /W4 /WX /permissive-)
    target_compile_options(octopoly_core_tests PRIVATE /W4 /WX /permissive-)
    target_compile_options(project_codec_tests PRIVATE /W4 /WX /permissive-)
    target_compile_options(mesh_allocation_fault_tests PRIVATE /W4 /WX /permissive-)
    target_compile_options(glb_codec_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(octopoly_core PRIVATE -Wall -Wextra -Wpedantic -Werror)
    target_compile_options(octopoly_core_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    target_compile_options(project_codec_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    target_compile_options(mesh_allocation_fault_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    target_compile_options(glb_codec_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

enable_testing()
add_test(NAME octopoly_core_tests COMMAND octopoly_core_tests)
add_test(NAME project_codec_tests COMMAND project_codec_tests)
add_test(NAME mesh_allocation_fault_tests COMMAND mesh_allocation_fault_tests)
add_test(NAME glb_codec_tests COMMAND glb_codec_tests)
