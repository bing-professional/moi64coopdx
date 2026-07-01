cmake_minimum_required(VERSION 3.20)

project(browser_backend_cef LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_definitions(WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)

if (MSVC)
  add_compile_options(/MP)
endif()

if (NOT WIN32)
  message(FATAL_ERROR "browser_backend_cef only supports Windows builds.")
endif()

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(CEF_ROOT "${REPO_ROOT}/lib/cef/win64" CACHE PATH "CEF SDK root")
set(CEF_OUTPUT_DIR "${CEF_ROOT}/bridge" CACHE PATH "Output directory for bridge artifacts")

if (NOT EXISTS "${CEF_ROOT}/include/cef_app.h")
  message(FATAL_ERROR "CEF headers were not found at ${CEF_ROOT}/include")
endif()

if (NOT EXISTS "${CEF_ROOT}/Release/libcef.lib")
  message(FATAL_ERROR "libcef.lib was not found at ${CEF_ROOT}/Release")
endif()

file(MAKE_DIRECTORY "${CEF_OUTPUT_DIR}")

function(SET_LIBRARY_TARGET_PROPERTIES target)
  target_include_directories(${target} PUBLIC "${CEF_ROOT}" "${CEF_ROOT}/include")
  target_compile_features(${target} PUBLIC cxx_std_20)
  target_compile_definitions(${target} PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
endfunction()

function(set_bridge_output_dirs target)
  set_target_properties(${target} PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CEF_OUTPUT_DIR}"
    LIBRARY_OUTPUT_DIRECTORY "${CEF_OUTPUT_DIR}"
    RUNTIME_OUTPUT_DIRECTORY "${CEF_OUTPUT_DIR}"
  )

  foreach(config Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${config}" config_upper)
    set_target_properties(${target} PROPERTIES
      ARCHIVE_OUTPUT_DIRECTORY_${config_upper} "${CEF_OUTPUT_DIR}"
      LIBRARY_OUTPUT_DIRECTORY_${config_upper} "${CEF_OUTPUT_DIR}"
      RUNTIME_OUTPUT_DIRECTORY_${config_upper} "${CEF_OUTPUT_DIR}"
    )
  endforeach()
endfunction()

add_subdirectory("${CEF_ROOT}/libcef_dll" "${CMAKE_CURRENT_BINARY_DIR}/libcef_dll")

add_library(cef_binary SHARED IMPORTED GLOBAL)
set_target_properties(cef_binary PROPERTIES
  IMPORTED_IMPLIB "${CEF_ROOT}/Release/libcef.lib"
  IMPORTED_LOCATION "${CEF_ROOT}/Release/libcef.dll"
)

add_library(browser_backend_cef SHARED
  src/browser_backend_cef.cpp
  "${REPO_ROOT}/src/pc/utils/miniaudio.c"
)
target_include_directories(browser_backend_cef PRIVATE
  "${REPO_ROOT}"
  "${REPO_ROOT}/include"
  "${REPO_ROOT}/src"
  "${CEF_ROOT}"
  "${CEF_ROOT}/include"
)
target_compile_definitions(browser_backend_cef PRIVATE
  WIN32_LEAN_AND_MEAN
  NOMINMAX
  UNICODE
  _UNICODE
  MA_NO_DECODING
)
target_link_libraries(browser_backend_cef PRIVATE cef_binary libcef_dll_wrapper)
target_link_libraries(browser_backend_cef PRIVATE ole32 uuid winmm avrt)
set_bridge_output_dirs(browser_backend_cef)

add_library(browser_subprocess SHARED
  src/browser_subprocess.cpp
)
target_include_directories(browser_subprocess PRIVATE
  "${CEF_ROOT}"
  "${CEF_ROOT}/include"
)
target_compile_definitions(browser_subprocess PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
target_link_libraries(browser_subprocess PRIVATE cef_binary libcef_dll_wrapper)
set_bridge_output_dirs(browser_subprocess)
