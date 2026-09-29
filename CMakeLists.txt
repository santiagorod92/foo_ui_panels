cmake_minimum_required(VERSION 3.21)
project(foo_ui_panels CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# foobar2000 SDK lives OUTSIDE the repo, as siblings of the checkout (same layout foo_navidrome
# uses):  <SDK_ROOT>/foobar2000/{SDK,foobar2000_component_client,shared}  and  <SDK_ROOT>/pfc.
# build.sh fetches it there if missing. Override with -DSDK_ROOT=/path.
set(SDK_ROOT "${CMAKE_SOURCE_DIR}/.." CACHE PATH "Directory containing foobar2000/ and pfc/")
get_filename_component(SDK "${SDK_ROOT}" ABSOLUTE)
foreach(_req pfc foobar2000/SDK foobar2000/foobar2000_component_client foobar2000/shared/shared-x64.lib)
  if(NOT EXISTS "${SDK}/${_req}")
    message(FATAL_ERROR "foobar2000 SDK not found: ${SDK}/${_req} is missing (run ./build.sh, or set -DSDK_ROOT)")
  endif()
endforeach()

# Component version (semantic-release writes version.txt on release; dev builds read the last one).
file(STRINGS "${CMAKE_SOURCE_DIR}/version.txt" PUI_VERSION LIMIT_COUNT 1)
if(NOT PUI_VERSION)
  set(PUI_VERSION "0.0.0-dev")
endif()

# Common defines for the fb2k SDK on Windows
add_compile_definitions(
  UNICODE _UNICODE
  WIN32 _WINDOWS
  _CRT_SECURE_NO_WARNINGS
  NDEBUG
  NOMINMAX) # windows.h's min()/max() macros otherwise mangle std::min/std::max call sites
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
target_compile_definitions(foo_ui_panels PRIVATE PUI_VERSION="${PUI_VERSION}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/version.txt")
set_target_properties(foo_ui_panels PROPERTIES PREFIX "" SUFFIX ".dll")
target_link_libraries(foo_ui_panels PRIVATE
  fb2ksdk pfc
  "${SDK}/foobar2000/shared/shared-x64.lib"
  user32 gdi32 shell32 ole32 oleaut32 uuid comctl32 comdlg32 advapi32 shlwapi winmm msimg32 gdiplus)
