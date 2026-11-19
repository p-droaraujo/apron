cmake_minimum_required(VERSION 3.25)
project(apron VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Núcleo sem dependências externas: JSON, eventos, tarefas, checks.
add_library(apron_core
  src/core/json.cpp
  src/core/events.cpp
  src/core/project.cpp
  src/core/time.cpp
  src/core/runs.cpp
)
target_include_directories(apron_core PUBLIC src)

add_executable(apron
  src/cli/main.cpp
  src/cli/cmd_run.cpp
  src/cli/cmd_report.cpp
  src/cli/cmd_log.cpp
)
target_link_libraries(apron PRIVATE apron_core)

enable_testing()
add_executable(apron_tests tests/test_main.cpp tests/test_json.cpp tests/test_events.cpp)
target_link_libraries(apron_tests PRIVATE apron_core)
add_test(NAME core COMMAND apron_tests)
