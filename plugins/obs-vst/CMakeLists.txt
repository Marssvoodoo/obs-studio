cmake_minimum_required(VERSION 3.28...3.30)

option(ENABLE_VST "Enable building OBS with VST plugin" ON)

if(NOT ENABLE_VST)
  target_disable(obs-vst)
  return()
endif()

add_library(obs-vst MODULE)
add_library(OBS::vst ALIAS obs-vst)

find_package(Qt6 REQUIRED Widgets)

target_sources(
  obs-vst
  PRIVATE
    $<$<PLATFORM_ID:Darwin>:mac/EditorWidget-osx.mm>
    $<$<PLATFORM_ID:Darwin>:mac/VSTPlugin-osx.mm>
    $<$<PLATFORM_ID:Linux,FreeBSD,OpenBSD>:linux/EditorWidget-linux.cpp>
    $<$<PLATFORM_ID:Linux,FreeBSD,OpenBSD>:linux/VSTPlugin-linux.cpp>
    $<$<PLATFORM_ID:Windows>:win/EditorWidget-win.cpp>
    $<$<PLATFORM_ID:Windows>:win/VSTPlugin-win.cpp>
    EditorWidget.cpp
    headers/EditorWidget.h
    headers/vst-plugin-callbacks.hpp
    headers/VSTPlugin.h
    obs-vst.cpp
    VST2ScanCache.cpp
    VST2ScanCache.hpp
    vst_header/aeffectx.h
    VSTPlugin.cpp
)

target_include_directories(obs-vst PRIVATE vst_header)

target_link_libraries(
  obs-vst
  PRIVATE
    OBS::libobs
    Qt::Widgets
    "$<$<PLATFORM_ID:Darwin>:$<LINK_LIBRARY:FRAMEWORK,Cocoa.framework>>"
    "$<$<PLATFORM_ID:Darwin>:$<LINK_LIBRARY:FRAMEWORK,Foundation.framework>>"
)

if(OS_WINDOWS)
  configure_file(cmake/windows/obs-module.rc.in obs-vst.rc)
  target_sources(obs-vst PRIVATE obs-vst.rc)
  set_property(TARGET obs-vst APPEND PROPERTY AUTORCC_OPTIONS --format-version 1)

  add_executable(obs-vst2-scanner)
  target_sources(obs-vst2-scanner PRIVATE vst2-scanner-main.cpp VST2ScanCache.cpp VST2ScanCache.hpp)
  target_include_directories(obs-vst2-scanner PRIVATE vst_header)
  target_link_libraries(obs-vst2-scanner PRIVATE OBS::libobs)
  set_target_properties_obs(obs-vst2-scanner PROPERTIES FOLDER plugins/obs-vst)
  add_dependencies(obs-vst obs-vst2-scanner)
endif()

set_target_properties_obs(
  obs-vst
  PROPERTIES FOLDER plugins
             PREFIX ""
             AUTOMOC ON
             AUTOUIC ON
             AUTORCC ON
)
