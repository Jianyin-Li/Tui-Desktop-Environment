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

add_executable(tdesktop src/main.cpp)
target_link_libraries(tdesktop PRIVATE tdesktop_core)
target_compile_options(tdesktop PRIVATE -Wall -Wextra -Wpedantic)

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
endif()

install(TARGETS tdesktop RUNTIME DESTINATION bin)
