cmake_minimum_required(VERSION 3.25)
project(apron VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Core with no external dependencies: JSON, events, tasks, i18n.
add_library(apron_core
  src/core/json.cpp
  src/core/events.cpp
  src/core/project.cpp
  src/core/time.cpp
  src/core/runs.cpp
  src/core/tasks.cpp
  src/core/i18n.cpp
  src/core/checks.cpp
)

# The TUI: terminal, cell grid and the progress view.
add_library(apron_tui
  src/tui/term.cpp
  src/tui/screen.cpp
  src/tui/progress.cpp
)
target_link_libraries(apron_tui PUBLIC apron_core)
target_include_directories(apron_core PUBLIC src)

add_executable(apron
  src/cli/main.cpp
  src/cli/cmd_run.cpp
  src/cli/cmd_report.cpp
  src/cli/cmd_log.cpp
  src/cli/cmd_tasks.cpp
  src/cli/cmd_ui.cpp
  src/cli/cmd_check.cpp
)
target_link_libraries(apron PRIVATE apron_tui)

enable_testing()
add_executable(apron_tests tests/test_main.cpp tests/test_json.cpp tests/test_events.cpp tests/test_tasks.cpp tests/test_tui.cpp tests/test_i18n.cpp tests/test_checks.cpp)
target_link_libraries(apron_tests PRIVATE apron_tui)
target_compile_definitions(apron_tests PRIVATE APRON_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME core COMMAND apron_tests)
