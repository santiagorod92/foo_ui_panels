cmake_minimum_required(VERSION 3.21)
project(foo_ui_panels CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SDK "${CMAKE_SOURCE_DIR}/sdk")

# Common defines for the fb2k SDK on Windows
add_compile_definitions(
  UNICODE _UNICODE
  WIN32 _WINDOWS
  _CRT_SECURE_NO_WARNINGS
  NDEBUG)
add_compile_options(/W2 /wd4996 /GR)

# --- pfc (portable foundation classes) ---
file(GLOB PFC_SRC "${SDK}/pfc/*.cpp")
add_library(pfc STATIC ${PFC_SRC})
target_include_directories(pfc PUBLIC "${SDK}")

# --- foobar2000 SDK core + component client (no ATL/UI helpers in Phase 1) ---
file(GLOB SDK_SRC "${SDK}/foobar2000/SDK/*.cpp")
list(APPEND SDK_SRC "${SDK}/foobar2000/foobar2000_component_client/component_client.cpp")
add_library(fb2ksdk STATIC ${SDK_SRC})
target_include_directories(fb2ksdk PUBLIC "${SDK}" "${SDK}/foobar2000")
target_link_libraries(fb2ksdk PUBLIC pfc)

# --- our component ---
file(GLOB COMP_SRC CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/*.cpp")
add_library(foo_ui_panels SHARED ${COMP_SRC})
set_target_properties(foo_ui_panels PROPERTIES PREFIX "" SUFFIX ".dll")
target_link_libraries(foo_ui_panels PRIVATE
  fb2ksdk pfc
  "${SDK}/foobar2000/shared/shared-x64.lib"
  user32 gdi32 shell32 ole32 oleaut32 uuid comctl32 comdlg32 advapi32 shlwapi winmm msimg32 gdiplus)
