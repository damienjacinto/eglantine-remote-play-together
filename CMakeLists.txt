# SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later

# Builds on its own (cmake -S . -B build) or as extensions/<folder>
# inside an Eglantine source tree, where it is copied next to the Qt executable.
cmake_minimum_required(VERSION 3.22)

set(id dev.eglantine.remote-play-together)
set(standalone OFF)
if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(eglantine-remote-play-together LANGUAGES CXX)
    set(standalone ON)
endif()

# Steam's runtime library is loaded at runtime, so building needs nothing from Valve. To bundle
# it in the extension folder, point STEAM_API_LIBRARY at the file, or STEAMWORKS_SDK_DIR at a
# Steamworks SDK (1.65 or later) to take it from redistributable_bin/.
if (WIN32)
    set(steam_redist "win64/steam_api64.dll")
elseif (APPLE)
    set(steam_redist "osx/libsteam_api.dylib")
else()
    set(steam_redist "linux64/libsteam_api.so")
endif()
set(STEAMWORKS_SDK_DIR "${STEAMWORKS_SDK_DIR}" CACHE PATH
    "Optional Steamworks SDK folder (holds redistributable_bin/), to bundle Steam's library")
if (NOT STEAM_API_LIBRARY AND STEAMWORKS_SDK_DIR)
    set(STEAM_API_LIBRARY "${STEAMWORKS_SDK_DIR}/redistributable_bin/${steam_redist}")
endif()
set(STEAM_API_LIBRARY "${STEAM_API_LIBRARY}" CACHE FILEPATH
    "Optional Steam runtime library to bundle (${steam_redist} from a Steamworks SDK)")
if (STEAM_API_LIBRARY AND NOT EXISTS "${STEAM_API_LIBRARY}")
    message(FATAL_ERROR "STEAM_API_LIBRARY does not exist: ${STEAM_API_LIBRARY}")
elseif (NOT STEAM_API_LIBRARY)
    message(WARNING "Steam's runtime library is not bundled: set STEAM_API_LIBRARY or "
                    "STEAMWORKS_SDK_DIR, or copy it next to the extension yourself")
endif()

add_library(remote-play-together MODULE remote_play_together.cpp)
if (TARGET eglantine_extension_api)
    target_link_libraries(remote-play-together PRIVATE eglantine_extension_api)
else()
    target_include_directories(remote-play-together PRIVATE include)
endif()
target_link_libraries(remote-play-together PRIVATE ${CMAKE_DL_LIBS})
target_compile_features(remote-play-together PRIVATE cxx_std_20)

set_target_properties(remote-play-together PROPERTIES
    PREFIX ""
    OUTPUT_NAME remote_play_together
    CXX_VISIBILITY_PRESET hidden)

# The extension folder: library, manifest and Steam's runtime library, which the extension loads
# from its own folder.
if (TARGET yuzu)
    set(destination "$<TARGET_FILE_DIR:yuzu>/extensions/${id}")
else()
    set(destination "${CMAKE_BINARY_DIR}/${id}")
endif()
add_custom_command(TARGET remote-play-together POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${destination}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:remote-play-together>" "${destination}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/extension.json" "${destination}"
    VERBATIM)
if (STEAM_API_LIBRARY)
    add_custom_command(TARGET remote-play-together POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${STEAM_API_LIBRARY}" "${destination}"
        VERBATIM)
endif()
