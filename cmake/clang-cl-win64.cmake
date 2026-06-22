# Cross-compile toolchain: Linux clang-cl -> Windows x64 .dll
# Uses Windows SDK/CRT splatted by xwin into ~/.xwin
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR AMD64)

# xwin ships only release CRT libs -> force static release runtime (/MT), no msvcrtd.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded" CACHE STRING "")
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

set(XWIN "$ENV{HOME}/.xwin")

set(CMAKE_C_COMPILER   clang-cl)
set(CMAKE_CXX_COMPILER clang-cl)
set(CMAKE_LINKER       lld-link)
set(CMAKE_RC_COMPILER  llvm-rc)
set(CMAKE_MT           llvm-mt)

# Target triple + header search (clang-cl reads /imsvc as system includes)
set(_INCS
  "/imsvc${XWIN}/crt/include"
  "/imsvc${XWIN}/sdk/include/ucrt"
  "/imsvc${XWIN}/sdk/include/um"
  "/imsvc${XWIN}/sdk/include/shared")
string(JOIN " " _INC_FLAGS ${_INCS})

set(_COMMON "--target=x86_64-pc-windows-msvc -fms-compatibility -fms-extensions ${_INC_FLAGS}")
set(CMAKE_C_FLAGS_INIT   "${_COMMON}")
set(CMAKE_CXX_FLAGS_INIT "${_COMMON} /EHsc")

set(_LIBDIRS
  "/libpath:${XWIN}/crt/lib/x86_64"
  "/libpath:${XWIN}/sdk/lib/ucrt/x86_64"
  "/libpath:${XWIN}/sdk/lib/um/x86_64")
string(JOIN " " _LIB_FLAGS ${_LIBDIRS})
set(CMAKE_EXE_LINKER_FLAGS_INIT    "${_LIB_FLAGS}")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${_LIB_FLAGS}")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
