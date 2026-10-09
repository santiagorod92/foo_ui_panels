cmake_minimum_required(VERSION 3.21)
project(foo_ui_panels CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# foobar2000 SDK lives OUTSIDE the repo, as siblings of the checkout (same layout foo_navidrome
# uses):  <SDK_ROOT>/foobar2000/{SDK,foobar2000_component_client,shared}  and  <SDK_ROOT>/pfc.
# build.sh fetches it there if missing. Override with -DSDK_ROOT=/path.
set(SDK_ROOT "${CMAKE_SOURCE_DIR}/.." CACHE PATH "Directory containing foobar2000/ and pfc/")
get_filename_component(SDK "${SDK_ROOT}" ABSOLUTE)
foreach(_req pfc foobar2000/SDK foobar2000/foobar2000_component_client foobar2000/shared)
  if(NOT EXISTS "${SDK}/${_req}")
    message(FATAL_ERROR "foobar2000 SDK not found: ${SDK}/${_req} is missing (run ./build.sh, or set -DSDK_ROOT)")
  endif()
endforeach()

# Component version: scripts/version.sh ($PUI_VERSION from the release jobs, else git describe,
# e.g. 1.6.0-dev.2+ef96f46). Regenerated on every build into version_generated.h (rewritten only
# on change, so component.cpp recompiles only when the version moves). The bundle's Info.plist is
# fixed at configure time and takes a plain x.y.z: the release's own, else the last release tag.
set(PUI_GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${PUI_GENERATED_DIR}")
if(DEFINED ENV{PUI_VERSION})
  set(PUI_BUNDLE_VERSION "$ENV{PUI_VERSION}")
else()
  execute_process(COMMAND bash "${CMAKE_SOURCE_DIR}/scripts/version.sh" --base
    OUTPUT_VARIABLE PUI_BUNDLE_VERSION OUTPUT_STRIP_TRAILING_WHITESPACE)
endif()

if(WIN32)
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
  # --- foobar2000 SDK core + component client (no ATL/UI helpers) ---
  file(GLOB SDK_SRC "${SDK}/foobar2000/SDK/*.cpp")
  list(APPEND SDK_SRC "${SDK}/foobar2000/foobar2000_component_client/component_client.cpp")
  set(PLATFORM_DIR win)
else()
  # macOS: same source sets the SDK's own .xcodeproj targets build.
  enable_language(OBJC OBJCXX)
  add_compile_definitions(NDEBUG)
  add_compile_options(-Wno-deprecated-declarations -Wno-unused-parameter -fvisibility=hidden)
  set(CMAKE_OBJCXX_STANDARD 20)
  set(PFC_NAMES SmartStrStr audio_math audio_sample base64 bigmem bit_array bsearch charDownConvert
    cpuid crashWithMessage filehandle filetimetools guid nix-objects other pathUtils printf selftest
    sort splitString2 string-compare string-conv-lite string-lite string_base string_conv
    synchro_nix threads timers unicode-normalize utf8 wildcard)
  set(PFC_SRC "${SDK}/pfc/obj-c.mm")
  foreach(n ${PFC_NAMES})
    list(APPEND PFC_SRC "${SDK}/pfc/${n}.cpp")
  endforeach()
  set(SDK_NAMES abort_callback advconfig album_art app_close_blocker audio_chunk
    audio_chunk_channel_config cfg_var chapterizer commonObjects completion_notify configStore
    config_io_callback config_object console dsp dsp_manager file_cached_impl file_info
    file_info_const_impl file_info_impl file_info_merge file_operation_callback filesystem
    filesystem_helper foosort fsItem guids hasher_md5 image input input_file_type link_resolver
    main_thread_callback mainmenu mem_block_container menu_helpers menu_item menu_manager metadb
    metadb_handle metadb_handle_list output packet_decoder playable_location playback_control
    playlist playlist_loader popup_message preferences_page replaygain replaygain_info service
    tag_processor tag_processor_id3v2 threaded_process titleformat track_property ui ui_element
    utility)
  set(SDK_SRC "${SDK}/foobar2000/SDK/apple-tools.mm" "${SDK}/foobar2000/SDK/commonObjects-Apple.mm"
    "${SDK}/foobar2000/foobar2000_component_client/component_client.cpp"
    "${SDK}/foobar2000/shared/audio_math.cpp" "${SDK}/foobar2000/shared/shared-apple.mm"
    "${SDK}/foobar2000/shared/shared-nix.cpp" "${SDK}/foobar2000/shared/utf8.cpp")
  foreach(n ${SDK_NAMES})
    list(APPEND SDK_SRC "${SDK}/foobar2000/SDK/${n}.cpp")
  endforeach()
  set(PLATFORM_DIR mac)
endif()

# The SDK is third-party code we don't patch: its headers are SYSTEM includes (no warnings from
# them in our sources) and its own .cpp files build with warnings off, so the build log only
# shows warnings in our code.
add_library(pfc STATIC ${PFC_SRC})
target_include_directories(pfc SYSTEM PUBLIC "${SDK}")
add_library(fb2ksdk STATIC ${SDK_SRC})
target_include_directories(fb2ksdk SYSTEM PUBLIC "${SDK}" "${SDK}/foobar2000")
target_link_libraries(fb2ksdk PUBLIC pfc)
foreach(t pfc fb2ksdk)
  target_compile_options(${t} PRIVATE $<IF:$<BOOL:${WIN32}>,/w,-w>)
endforeach()

if(APPLE)
  # The SDK's own targets build with ARC (CLANG_ENABLE_OBJC_ARC = YES in foobar2000_SDK.xcodeproj
  # and pfc.xcodeproj), so their Obj-C sources rely on it for ownership. Without it,
  # NSObjectWrapperImpl's `id obj` (SDK/commonObjects-Apple.mm) is a non-retaining raw pointer:
  # fb2k::wrapNSObject() then keeps a dangling reference to an object our ARC code releases on
  # return, and the host crashes in objc_retain when it later unwraps it (e.g. the NSViewController
  # returned by ui_element_mac::instantiate).
  foreach(t pfc fb2ksdk)
    target_compile_options(${t} PRIVATE $<$<COMPILE_LANGUAGE:OBJC,OBJCXX>:-fobjc-arc>)
  endforeach()
endif()

# --- our component ---
# Platform-free code (skin interpreter, panels, gfx/ui interfaces) + this platform's backend.
file(GLOB COMP_SRC CONFIGURE_DEPENDS
  "${CMAKE_SOURCE_DIR}/src/core/*.cpp"
  "${CMAKE_SOURCE_DIR}/src/panels/*.cpp"
  "${CMAKE_SOURCE_DIR}/src/platform/${PLATFORM_DIR}/*.cpp"
  "${CMAKE_SOURCE_DIR}/src/platform/${PLATFORM_DIR}/*.mm")
# Images built into the component (res/, e.g. the layout wizard's previews): src/core/builtin_images.h.
include(cmake/embed_images.cmake)
pui_embed_images("${CMAKE_SOURCE_DIR}/res" "${PUI_GENERATED_DIR}/builtin_images.cpp")
list(APPEND COMP_SRC "${PUI_GENERATED_DIR}/builtin_images.cpp")

if(WIN32)
  add_library(foo_ui_panels SHARED ${COMP_SRC})
  set_target_properties(foo_ui_panels PROPERTIES PREFIX "" SUFFIX ".dll")
  target_link_libraries(foo_ui_panels PRIVATE
    fb2ksdk pfc
    "${SDK}/foobar2000/shared/shared-x64.lib"
    user32 gdi32 shell32 ole32 oleaut32 uuid comctl32 comdlg32 advapi32 shlwapi winmm msimg32 gdiplus uxtheme)
else()
  # A loadable bundle: foo_ui_panels.component/Contents/{Info.plist,MacOS/foo_ui_panels}.
  add_library(foo_ui_panels MODULE ${COMP_SRC})
  set_target_properties(foo_ui_panels PROPERTIES
    BUNDLE TRUE
    BUNDLE_EXTENSION component
    MACOSX_BUNDLE_BUNDLE_NAME foo_ui_panels
    MACOSX_BUNDLE_GUI_IDENTIFIER com.santirod.foo-ui-panels
    MACOSX_BUNDLE_BUNDLE_VERSION "${PUI_BUNDLE_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PUI_BUNDLE_VERSION}"
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/cmake/mac/Info.plist.in")
  target_compile_options(foo_ui_panels PRIVATE -fobjc-arc)
  target_link_libraries(foo_ui_panels PRIVATE fb2ksdk pfc
    "-framework Cocoa" "-framework CoreGraphics" "-framework CoreText" "-framework ImageIO"
    "-framework QuartzCore" "-framework CoreFoundation" "-framework Security")
endif()
add_custom_target(pui_version
  COMMAND bash "${CMAKE_SOURCE_DIR}/scripts/version.sh" --header "${PUI_GENERATED_DIR}/version_generated.h"
  BYPRODUCTS "${PUI_GENERATED_DIR}/version_generated.h"
  COMMENT "Component version"
  VERBATIM)
add_dependencies(foo_ui_panels pui_version)
target_include_directories(foo_ui_panels PRIVATE "${PUI_GENERATED_DIR}")
set_target_properties(foo_ui_panels PROPERTIES EXPORT_COMPILE_COMMANDS ON)
