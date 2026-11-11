# SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later

# Builds on its own (cmake -S . -B build -DSTEAMWORKS_SDK_DIR=<sdk>) or as extensions/<folder>
# inside an Eglantine source tree, where it is copied next to the Qt executable.
cmake_minimum_required(VERSION 3.22)

set(id dev.eglantine.remote-play-together)
set(standalone OFF)
if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(eglantine-remote-play-together LANGUAGES CXX)
    set(standalone ON)
endif()

set(STEAMWORKS_SDK_DIR "${STEAMWORKS_SDK_DIR}" CACHE PATH
    "Steamworks SDK folder (holds public/ and redistributable_bin/)")
set(steam_public "${STEAMWORKS_SDK_DIR}/public")
if (NOT EXISTS "${steam_public}/steam/isteamremoteplay.h")
    message(FATAL_ERROR "Set STEAMWORKS_SDK_DIR to a Steamworks SDK (the folder holding public/steam)")
endif()
file(READ "${steam_public}/steam/isteamremoteplay.h" remote_play_header)
if (NOT remote_play_header MATCHES "BEnableRemotePlayTogetherDirectInput")
    message(FATAL_ERROR "The Steamworks SDK in STEAMWORKS_SDK_DIR is older than 1.62")
endif()

set(steam_bin "${STEAMWORKS_SDK_DIR}/redistributable_bin")
if (WIN32)
    set(steam_link "${steam_bin}/win64/steam_api64.lib")
    set(steam_runtime "${steam_bin}/win64/steam_api64.dll")
elseif (APPLE)
    set(steam_link "${steam_bin}/osx/libsteam_api.dylib")
    set(steam_runtime "${steam_link}")
else()
    set(steam_link "${steam_bin}/linux64/libsteam_api.so")
    set(steam_runtime "${steam_link}")
endif()

add_library(remote-play-together MODULE remote_play_together.cpp)
if (TARGET eglantine_extension_api)
    target_link_libraries(remote-play-together PRIVATE eglantine_extension_api)
else()
    target_include_directories(remote-play-together PRIVATE include)
endif()
target_include_directories(remote-play-together SYSTEM PRIVATE "${steam_public}")
target_link_libraries(remote-play-together PRIVATE "${steam_link}")
target_compile_features(remote-play-together PRIVATE cxx_std_20)

if (APPLE)
    set(origin "@loader_path")
else()
    set(origin "$ORIGIN")
endif()
set_target_properties(remote-play-together PROPERTIES
    PREFIX ""
    OUTPUT_NAME remote_play_together
    CXX_VISIBILITY_PRESET hidden
    BUILD_RPATH "${origin}"
    INSTALL_RPATH "${origin}")

# The extension folder: library, manifest and Steam's runtime library, which the loader finds
# next to the extension.
if (TARGET yuzu)
    set(destination "$<TARGET_FILE_DIR:yuzu>/extensions/${id}")
else()
    set(destination "${CMAKE_BINARY_DIR}/${id}")
endif()
add_custom_command(TARGET remote-play-together POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${destination}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:remote-play-together>" "${destination}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/extension.json" "${destination}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${steam_runtime}" "${destination}"
    VERBATIM)
