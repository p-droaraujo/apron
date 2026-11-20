cmake_minimum_required(VERSION 3.25)

# On macOS, CMake sometimes does not find the SDK (seen with AppleClang 21 and
# the Command Line Tools), and then not even <string> is found. Ask xcrun for
# it before project(), unless it was given explicitly.
if(NOT DEFINED CMAKE_OSX_SYSROOT AND NOT DEFINED ENV{SDKROOT})
  find_program(APRON_XCRUN xcrun)
  if(APRON_XCRUN)
    execute_process(COMMAND ${APRON_XCRUN} --show-sdk-path
                    OUTPUT_VARIABLE APRON_SDK OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(APRON_SDK)
      set(CMAKE_OSX_SYSROOT "${APRON_SDK}" CACHE PATH "macOS SDK")
    endif()
  endif()
endif()

project(apron VERSION 0.0.1 LANGUAGES CXX)

# Optimized unless asked otherwise: a Debug build is about 20x slower here.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Core: JSON, events, tasks, i18n. Only the standard library.
add_library(apron_core
  src/core/json.cpp
  src/core/events.cpp
  src/core/project.cpp
  src/core/time.cpp
  src/core/runs.cpp
  src/core/tasks.cpp
  src/core/i18n.cpp
  src/core/checks.cpp
  src/core/scope.cpp
  src/core/review.cpp
  src/core/activity.cpp
  src/core/board.cpp
)

# libghostty-vt (D5): the terminal emulation behind Focus and Grid. The build
# fetches the libghostty-vt release vt-1.0.1 and runs zig on it, so zig 0.17
# must be on the PATH (`brew install zig`). Moving to a newer release is how
# Apron follows the Zig that Homebrew ships. FETCHCONTENT_SOURCE_DIR_GHOSTTY
# points at a local checkout instead.
include(FetchContent)
set(GHOSTTY_ZIG_BUILD_FLAGS "-Dapp-runtime=none;-Demit-xcframework=false" CACHE STRING "" FORCE)
FetchContent_Declare(ghostty
  GIT_REPOSITORY https://github.com/ghostty-org/ghostty.git
  GIT_TAG 108cacb17ed8990840cd14abfce43d8ed8503ca1  # vt-1.0.1
)
FetchContent_MakeAvailable(ghostty)

# The TUI: terminal, cell grid and the progress view.
add_library(apron_tui
  src/tui/term.cpp
  src/tui/screen.cpp
  src/tui/progress.cpp
  src/tui/app.cpp
  src/tui/vt.cpp
)
target_link_libraries(apron_tui PUBLIC apron_core PRIVATE ghostty-vt-static)
target_include_directories(apron_core PUBLIC src)

# Protocol and daemon.
add_library(apron_proto src/proto/frame.cpp src/proto/socket.cpp)
target_include_directories(apron_proto PUBLIC src)

add_executable(aprond src/daemon/main.cpp src/daemon/server.cpp src/daemon/session.cpp src/daemon/registry.cpp)
target_link_libraries(aprond PRIVATE apron_core apron_proto)
if(NOT APPLE)
  target_link_libraries(aprond PRIVATE util)  # forkpty
endif()

add_executable(apron
  src/cli/main.cpp
  src/cli/cmd_run.cpp
  src/cli/cmd_report.cpp
  src/cli/cmd_log.cpp
  src/cli/cmd_tasks.cpp
  src/cli/cmd_ui.cpp
  src/cli/cmd_check.cpp
  src/cli/cmd_sessions.cpp
  src/cli/cmd_hook.cpp
  src/cli/cmd_machines.cpp
  src/cli/daemon_client.cpp
  src/cli/fleet.cpp
)
target_link_libraries(apron PRIVATE apron_tui apron_proto)

enable_testing()
add_executable(apron_tests tests/test_main.cpp tests/test_json.cpp tests/test_events.cpp tests/test_tasks.cpp tests/test_tui.cpp tests/test_i18n.cpp tests/test_checks.cpp tests/test_scope.cpp tests/test_daemon.cpp tests/test_cli.cpp tests/test_hooks.cpp tests/test_review.cpp)
target_link_libraries(apron_tests PRIVATE apron_tui apron_proto)
add_dependencies(apron_tests aprond apron)
target_compile_definitions(apron_tests PRIVATE APRON_SOURCE_DIR="${CMAKE_SOURCE_DIR}" APRON_BINARY_DIR="${CMAKE_BINARY_DIR}")
add_test(NAME core COMMAND apron_tests)

# cmake --install build --prefix ~/.local   puts apron and aprond side by side in ~/.local/bin
install(TARGETS apron aprond RUNTIME DESTINATION bin)
