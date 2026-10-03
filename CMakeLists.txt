cmake_minimum_required(VERSION 3.23)
project(Coop VERSION 0.0.1 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)

set(RED4EXT_HEADER_ONLY ON CACHE BOOL "" FORCE)
add_subdirectory(vendor/RED4ext.SDK)
add_subdirectory(vendor/RedLib)
add_subdirectory(vendor/enet EXCLUDE_FROM_ALL)

add_library(Coop SHARED src/Main.cpp)
target_link_libraries(Coop PRIVATE RED4ext::SDK RedLib enet)
if(MSVC)
  target_compile_options(Coop PRIVATE /W4 /WX)
endif()

# Post-build deploy to the game folder (env var CP2077_DIR)
if(DEFINED ENV{CP2077_DIR})
  set(_plug "$ENV{CP2077_DIR}/red4ext/plugins/Coop")
  add_custom_command(TARGET Coop POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_plug}"
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:Coop> "${_plug}/"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/scripts" "${_plug}/scripts"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/cet/coop"
            "$ENV{CP2077_DIR}/bin/x64/plugins/cyber_engine_tweaks/mods/coop")
endif()
