cmake_minimum_required(VERSION 3.20)

project(tdesktop
  VERSION 0.1.0
  DESCRIPTION "A TTY-first desktop environment"
  LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Threads REQUIRED)
find_package(ftxui CONFIG REQUIRED)
find_path(VTERM_INCLUDE_DIR vterm.h)
find_library(VTERM_LIBRARY NAMES vterm libvterm)
find_library(UTIL_LIBRARY util)

if(NOT VTERM_INCLUDE_DIR OR NOT VTERM_LIBRARY)
  message(FATAL_ERROR
    "libvterm not found. Install libvterm-dev or pass "
    "-DVTERM_INCLUDE_DIR=... -DVTERM_LIBRARY=...")
endif()

get_filename_component(VTERM_LIBRARY_DIR "${VTERM_LIBRARY}" DIRECTORY)

configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/src/version.h.in"
  "${CMAKE_CURRENT_BINARY_DIR}/generated/tdesktop/version.h"
  @ONLY
)

add_library(tdesktop_core STATIC
  src/platform/environment.cpp
  src/platform/launch_context.cpp
)
target_include_directories(tdesktop_core
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_BINARY_DIR}/generated"
)
target_compile_options(tdesktop_core PRIVATE -Wall -Wextra -Wpedantic)

add_library(tdesktop_ui STATIC
  src/terminal/pty_session.cpp
  src/ui/terminal_view.cpp
  src/ui/terminal_app.cpp
)
target_include_directories(tdesktop_ui
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_BINARY_DIR}/generated"
    "${VTERM_INCLUDE_DIR}"
)
target_link_libraries(tdesktop_ui
  PUBLIC
    tdesktop_core
    ftxui::component
    Threads::Threads
    "${VTERM_LIBRARY}"
)
if(UTIL_LIBRARY)
  target_link_libraries(tdesktop_ui PUBLIC "${UTIL_LIBRARY}")
endif()
target_compile_options(tdesktop_ui PRIVATE -Wall -Wextra -Wpedantic)

add_executable(tdesktop src/main.cpp)
target_link_libraries(tdesktop PRIVATE tdesktop_ui)
target_compile_options(tdesktop PRIVATE -Wall -Wextra -Wpedantic)
set_target_properties(tdesktop PROPERTIES
  BUILD_RPATH "${VTERM_LIBRARY_DIR}"
)

include(CTest)
if(BUILD_TESTING)
  add_executable(environment_test tests/environment_test.cpp)
  target_link_libraries(environment_test PRIVATE tdesktop_core)
  target_include_directories(environment_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/tests")
  target_compile_options(environment_test PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME environment_test COMMAND environment_test)

  add_executable(launch_context_test tests/launch_context_test.cpp)
  target_link_libraries(launch_context_test PRIVATE tdesktop_core)
  target_include_directories(launch_context_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/tests")
  target_compile_options(launch_context_test PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME launch_context_test COMMAND launch_context_test)

  add_executable(vterm_ansi_test tests/vterm_ansi_test.cpp)
  target_link_libraries(vterm_ansi_test PRIVATE "${VTERM_LIBRARY}")
  target_include_directories(vterm_ansi_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/tests"
    "${VTERM_INCLUDE_DIR}"
  )
  target_compile_options(vterm_ansi_test PRIVATE -Wall -Wextra -Wpedantic)
  set_target_properties(vterm_ansi_test PROPERTIES
    BUILD_RPATH "${VTERM_LIBRARY_DIR}"
  )
  add_test(NAME vterm_ansi_test COMMAND vterm_ansi_test)
endif()

install(TARGETS tdesktop RUNTIME DESTINATION bin)
