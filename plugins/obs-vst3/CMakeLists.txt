cmake_minimum_required(VERSION 3.28...3.30)

option(ENABLE_VST3 "Enable building OBS with the experimental VST3 audio filter" ON)

if(NOT ENABLE_VST3)
  target_disable(obs-vst3)
  return()
endif()

find_package(Qt6 REQUIRED Core Widgets)
find_package(VST3SDK QUIET)

if(NOT VST3SDK_FOUND)
  message(STATUS "VST3 SDK not found - disabling obs-vst3 plugin.")
  target_disable(obs-vst3)
  return()
endif()

add_library(obs-vst3 MODULE)
add_library(OBS::vst3 ALIAS obs-vst3)

set(VST3_SDK_SOURCES)
foreach(source IN LISTS VST3SDK_REQUIRED_FILES)
  list(APPEND VST3_SDK_SOURCES "${VST3SDK_PATH}/${source}")
endforeach()

add_library(obs-vst3-hosting STATIC)
target_sources(
  obs-vst3-hosting
  PRIVATE
    ${VST3_SDK_SOURCES}
    "${CMAKE_SOURCE_DIR}/shared/plugin-path-fingerprint/PluginPathFingerprint.cpp"
    VST3Cache.cpp
    VST3Cache.h
    VST3HostApp.cpp
    VST3HostApp.h
    VST3Scanner.cpp
    VST3Scanner.h
)

target_include_directories(
  obs-vst3-hosting
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/shared/plugin-path-fingerprint
    ${VST3SDK_PATH}
    ${VST3SDK_PATH}/base
    ${VST3SDK_PATH}/pluginterfaces
    ${VST3SDK_PATH}/public.sdk/source/vst
    ${VST3SDK_PATH}/public.sdk/source/vst/hosting
    ${VST3SDK_PATH}/public.sdk/source/vst/utility
)

target_link_libraries(obs-vst3-hosting PUBLIC OBS::libobs Qt::Core VST3::SDK)
set_target_properties(obs-vst3-hosting PROPERTIES FOLDER plugins/obs-vst3 POSITION_INDEPENDENT_CODE ON)

add_executable(obs-vst3-scanner)
target_sources(obs-vst3-scanner PRIVATE scanner-main.cpp)
target_link_libraries(obs-vst3-scanner PRIVATE obs-vst3-hosting OBS::libobs)
set_target_properties_obs(obs-vst3-scanner PROPERTIES FOLDER plugins/obs-vst3)

target_sources(
  obs-vst3
  PRIVATE
    editor/win/VST3EditorWindow.cpp
    obs-vst3.cpp
    obs-vst3.h
    plugin-main.cpp
    VST3ComponentHolder.cpp
    VST3ComponentHolder.h
    VST3EditorWindow.h
    VST3Plugin.cpp
    VST3Plugin.h
)

target_include_directories(
  obs-vst3
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${VST3SDK_PATH}
    ${VST3SDK_PATH}/base
    ${VST3SDK_PATH}/pluginterfaces
    ${VST3SDK_PATH}/public.sdk/source/vst
    ${VST3SDK_PATH}/public.sdk/source/vst/hosting
    ${VST3SDK_PATH}/public.sdk/source/vst/utility
)

target_link_libraries(obs-vst3 PRIVATE obs-vst3-hosting OBS::libobs Qt::Widgets VST3::SDK)
add_dependencies(obs-vst3 obs-vst3-scanner)

set_target_properties_obs(obs-vst3 PROPERTIES FOLDER plugins PREFIX "" AUTOMOC ON)
