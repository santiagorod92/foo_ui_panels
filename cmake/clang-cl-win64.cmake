set(CMAKE_SYSTEM_NAME Windows)
set(WIN_ARCH x64 CACHE STRING "Windows target: x64 or arm64ec")
set_property(CACHE WIN_ARCH PROPERTY STRINGS x64 arm64ec)
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES WIN_ARCH)
if(WIN_ARCH STREQUAL "arm64ec")
  set(CMAKE_SYSTEM_PROCESSOR ARM64EC)
  set(_TRIPLE arm64ec-pc-windows-msvc)
  set(_LIBARCH aarch64)
  set(_UCRTARCH x86_64)
  set(_MACHINE "/machine:arm64ec")
elseif(WIN_ARCH STREQUAL "x64")
  set(CMAKE_SYSTEM_PROCESSOR AMD64)
  set(_TRIPLE x86_64-pc-windows-msvc)
  set(_LIBARCH x86_64)
  set(_UCRTARCH x86_64)
  set(_MACHINE "")
else()
  message(FATAL_ERROR "WIN_ARCH must be x64 or arm64ec (got '${WIN_ARCH}')")
endif()

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded" CACHE STRING "")
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

if(DEFINED ENV{XWIN})
  set(XWIN "$ENV{XWIN}")
else()
  set(XWIN "$ENV{HOME}/.xwin")
endif()

set(CMAKE_C_COMPILER   clang-cl)
set(CMAKE_CXX_COMPILER clang-cl)
set(CMAKE_LINKER       lld-link)
set(CMAKE_RC_COMPILER  llvm-rc)
set(CMAKE_MT           llvm-mt)

set(_INCS
  "/imsvc${XWIN}/crt/include"
  "/imsvc${XWIN}/sdk/include/ucrt"
  "/imsvc${XWIN}/sdk/include/um"
  "/imsvc${XWIN}/sdk/include/shared")
string(JOIN " " _INC_FLAGS ${_INCS})

set(_COMMON "--target=${_TRIPLE} -fms-compatibility -fms-extensions ${_INC_FLAGS}")
set(CMAKE_C_FLAGS_INIT   "${_COMMON}")
set(CMAKE_CXX_FLAGS_INIT "${_COMMON} /EHsc")

set(_LIBDIRS
  "/libpath:${XWIN}/crt/lib/${_LIBARCH}"
  "/libpath:${XWIN}/sdk/lib/ucrt/${_UCRTARCH}"
  "/libpath:${XWIN}/sdk/lib/um/${_LIBARCH}")
foreach(_d ${_LIBDIRS})
  string(REPLACE "/libpath:" "" _d "${_d}")
  if(NOT EXISTS "${_d}")
    message(FATAL_ERROR "${_d} missing — splat both arches: xwin --accept-license --arch x86_64,aarch64 splat --output ${XWIN}")
  endif()
endforeach()
string(JOIN " " _LIB_FLAGS ${_MACHINE} ${_LIBDIRS})
set(CMAKE_EXE_LINKER_FLAGS_INIT    "${_LIB_FLAGS}")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${_LIB_FLAGS}")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
