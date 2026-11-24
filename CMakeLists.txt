cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" ON)
option(ENABLE_QT "Use Qt functionality" ON)
option(ENABLE_TESTS "Build unit tests for the protocol code" OFF)

if(NOT ENABLE_FRONTEND_API OR NOT ENABLE_QT)
  message(FATAL_ERROR "${_name} is a dock plugin and requires ENABLE_FRONTEND_API and ENABLE_QT")
endif()

include(compilerconfig)
include(defaults)
include(helpers)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Network)
find_package(CURL REQUIRED)

# Vendored third-party code is built without the project's warnings-as-errors policy.
add_library(qrcodegen STATIC deps/qrcodegen/qrcodegen.cpp)
target_include_directories(qrcodegen PUBLIC deps)
set_target_properties(qrcodegen PROPERTIES COMPILE_WARNING_AS_ERROR OFF POSITION_INDEPENDENT_CODE ON)

# zlib's reference inflater: decompresses into a caller-sized buffer, so chat batches can't inflate without bound.
add_library(puff STATIC deps/puff/puff.c)
target_include_directories(puff PUBLIC deps)
set_target_properties(puff PROPERTIES COMPILE_WARNING_AS_ERROR OFF POSITION_INDEPENDENT_CODE ON)

# Protocol code without OBS dependencies, shared by the plugin and the tests.
add_library(bililive-core STATIC)
target_sources(
  bililive-core
  PRIVATE
    src/bili/api.cpp
    src/bili/http.cpp
    src/bili/sign.cpp
    src/danmaku/client.cpp
    src/danmaku/events.cpp
    src/danmaku/packet.cpp
    src/danmaku/protobuf.cpp
    src/update.cpp
)
target_include_directories(bililive-core PUBLIC src)
target_link_libraries(bililive-core PUBLIC Qt6::Core Qt6::Network CURL::libcurl qrcodegen puff)
set_target_properties(bililive-core PROPERTIES AUTOMOC ON POSITION_INDEPENDENT_CODE ON)

add_library(${CMAKE_PROJECT_NAME} MODULE)

find_package(libobs REQUIRED)
find_package(obs-frontend-api REQUIRED)
target_link_libraries(
  ${CMAKE_PROJECT_NAME}
  PRIVATE OBS::libobs OBS::obs-frontend-api Qt6::Core Qt6::Widgets bililive-core
)
target_compile_options(
  ${CMAKE_PROJECT_NAME}
  PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header -Wno-comma>
)
set_target_properties(
  ${CMAKE_PROJECT_NAME}
  PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON
)

target_sources(
  ${CMAKE_PROJECT_NAME}
  PRIVATE
    src/plugin-main.cpp
    src/settings.cpp
    src/stream-control.cpp
    src/ui/dock.cpp
    src/ui/overlay.cpp
    src/ui/qr-dialog.cpp
)

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})

if(ENABLE_TESTS)
  enable_testing()
  find_package(Qt6 REQUIRED COMPONENTS Test)
  add_executable(bililive-tests tests/test-protocol.cpp)
  target_link_libraries(bililive-tests PRIVATE bililive-core Qt6::Test)
  set_target_properties(bililive-tests PROPERTIES AUTOMOC ON)
  add_test(NAME bililive-tests COMMAND bililive-tests)
endif()
