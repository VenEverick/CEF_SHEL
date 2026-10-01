# SHELTER — приватный браузер на Chromium Embedded Framework.
cmake_minimum_required(VERSION 3.21)

set(CMAKE_CONFIGURATION_TYPES Release)
project(shelter)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

set(SHELTER_VERSION "1.0.163")

# ---- CEF ---------------------------------------------------------------------
set(CEF_VERSION "154.0.32+g682c378+chromium-154.0.8037.58")
set(CEF_CHANNEL "minimal")

if("${CMAKE_SYSTEM_NAME}" STREQUAL "Darwin")
  # Целевая платформа — Intel (x86_64).
  set(PROJECT_ARCH "x86_64")
  set(CEF_PLATFORM "macosx64")
elseif("${CMAKE_SYSTEM_NAME}" STREQUAL "Windows")
  set(CEF_PLATFORM "windows64")
else()
  message(FATAL_ERROR "SHELTER собирается только для macOS (x86_64) и Windows (x64).")
endif()

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(DownloadCEF)
DownloadCEF("${CEF_PLATFORM}" "${CEF_CHANNEL}" "${CEF_VERSION}" "${CMAKE_SOURCE_DIR}/third_party/cef")

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CEF_ROOT}/cmake")
find_package(CEF REQUIRED)

# Без песочницы (MVP): проще сборка/подпись, отдельная библиотека не нужна.
# (USE_SANDBOX задаётся в командной строке: -DUSE_SANDBOX=OFF)

# Предупреждения не должны ломать сборку.
list(REMOVE_ITEM CEF_COMPILER_FLAGS -Werror /WX)

add_subdirectory(${CEF_LIBCEF_DLL_WRAPPER_PATH} libcef_dll_wrapper)
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

# ---- Исходники ---------------------------------------------------------------
set(SHELTER_COMMON_SRCS
  src/common.cc src/common.h
  src/secret_key.cc
  src/renderer_app.cc src/renderer_app.h
  src/ui_scheme.cc src/ui_scheme.h
  src/key_map.cc src/key_map.h
  src/clients.cc src/clients.h
  src/shell.cc src/shell_bridge.cc src/shell.h
  src/browser_app.cc src/browser_app.h
  src/platform.h
)
set(SHELTER_SRCS_WINDOWS src/main_win.cc src/platform_win.cc win/shelter.rc)
set(SHELTER_SRCS_MAC src/main_mac.mm src/platform_mac.mm)
set(SHELTER_SRCS ${SHELTER_COMMON_SRCS})
APPEND_PLATFORM_SOURCES(SHELTER_SRCS)

set(SHELTER_UI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/resources/ui")
set(CEF_TARGET "Shelter")
SET_CEF_TARGET_OUT_DIR()

# ---- Windows -----------------------------------------------------------------
if(OS_WINDOWS)
  add_executable(${CEF_TARGET} WIN32 ${SHELTER_SRCS})
  SET_EXECUTABLE_TARGET_PROPERTIES(${CEF_TARGET})
  ADD_WINDOWS_MANIFEST("${CMAKE_CURRENT_SOURCE_DIR}/win" "${CEF_TARGET}" "exe")
  ADD_LOGICAL_TARGET("libcef_lib" "${CEF_LIB_DEBUG}" "${CEF_LIB_RELEASE}")
  add_dependencies(${CEF_TARGET} libcef_dll_wrapper)
  target_link_libraries(${CEF_TARGET} libcef_lib libcef_dll_wrapper ${CEF_STANDARD_LIBS}
                        shell32 ole32 user32 uuid crypt32)

  COPY_FILES("${CEF_TARGET}" "${CEF_BINARY_FILES}" "${CEF_BINARY_DIR}" "${CEF_TARGET_OUT_DIR}")
  COPY_FILES("${CEF_TARGET}" "${CEF_RESOURCE_FILES}" "${CEF_RESOURCE_DIR}" "${CEF_TARGET_OUT_DIR}")
  add_custom_command(TARGET ${CEF_TARGET} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${SHELTER_UI_DIR}" "${CEF_TARGET_OUT_DIR}/ui"
    VERBATIM)
endif()

# ---- macOS -------------------------------------------------------------------
if(OS_MAC)
  set(CEF_HELPER_TARGET "Shelter_Helper")
  set(CEF_HELPER_OUTPUT_NAME "Shelter Helper")

  set(CEF_APP "${CEF_TARGET_OUT_DIR}/${CEF_TARGET}.app")
  set(EXECUTABLE_NAME "${CEF_TARGET}")
  set(PRODUCT_NAME "${CEF_TARGET}")
  set(VERSION_SHORT "${SHELTER_VERSION}")

  add_executable(${CEF_TARGET} MACOSX_BUNDLE ${SHELTER_SRCS} mac/Info.plist.in)
  SET_EXECUTABLE_TARGET_PROPERTIES(${CEF_TARGET})
  add_dependencies(${CEF_TARGET} libcef_dll_wrapper)
  target_link_libraries(${CEF_TARGET} libcef_dll_wrapper ${CEF_STANDARD_LIBS}
                        "-framework Cocoa" "-framework QuartzCore")
  set_target_properties(${CEF_TARGET} PROPERTIES
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/mac/Info.plist.in)

  COPY_MAC_FRAMEWORK("${CEF_TARGET}" "${CEF_BINARY_DIR}" "${CEF_APP}")

  foreach(_suffix_list ${CEF_HELPER_APP_SUFFIXES})
    string(REPLACE ":" ";" _suffix_list ${_suffix_list})
    list(GET _suffix_list 0 _name_suffix)
    list(GET _suffix_list 1 _target_suffix)
    list(GET _suffix_list 2 _plist_suffix)

    set(_helper_target "${CEF_HELPER_TARGET}${_target_suffix}")
    set(_helper_output_name "${CEF_HELPER_OUTPUT_NAME}${_name_suffix}")

    set(_helper_info_plist "${CMAKE_CURRENT_BINARY_DIR}/helper-Info${_target_suffix}.plist")
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/mac/helper-Info.plist.in" _plist_contents)
    string(REPLACE "\${EXECUTABLE_NAME}" "${_helper_output_name}" _plist_contents ${_plist_contents})
    string(REPLACE "\${PRODUCT_NAME}" "${_helper_output_name}" _plist_contents ${_plist_contents})
    string(REPLACE "\${BUNDLE_ID_SUFFIX}" "${_plist_suffix}" _plist_contents ${_plist_contents})
    string(REPLACE "\${VERSION_SHORT}" "${SHELTER_VERSION}" _plist_contents ${_plist_contents})
    file(WRITE ${_helper_info_plist} ${_plist_contents})

    add_executable(${_helper_target} MACOSX_BUNDLE src/process_helper_mac.cc
                   src/renderer_app.cc src/renderer_app.h src/common.cc src/common.h)
    SET_EXECUTABLE_TARGET_PROPERTIES(${_helper_target})
    add_dependencies(${_helper_target} libcef_dll_wrapper)
    target_link_libraries(${_helper_target} libcef_dll_wrapper ${CEF_STANDARD_LIBS})
    set_target_properties(${_helper_target} PROPERTIES
      MACOSX_BUNDLE_INFO_PLIST ${_helper_info_plist}
      OUTPUT_NAME ${_helper_output_name})

    add_dependencies(${CEF_TARGET} "${_helper_target}")
    add_custom_command(TARGET ${CEF_TARGET} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_directory
              "${CEF_TARGET_OUT_DIR}/${_helper_output_name}.app"
              "${CEF_APP}/Contents/Frameworks/${_helper_output_name}.app"
      VERBATIM)
  endforeach()

  add_custom_command(TARGET ${CEF_TARGET} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CEF_APP}/Contents/Resources"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${SHELTER_UI_DIR}" "${CEF_APP}/Contents/Resources/ui"
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/mac/Shelter.icns" "${CEF_APP}/Contents/Resources/Shelter.icns"
    VERBATIM)
endif()

PRINT_CEF_CONFIG()
