add_executable(obs-fork-core-regressions core-regressions.c)
target_include_directories(obs-fork-core-regressions PRIVATE "${CMAKE_SOURCE_DIR}/libobs")
set_target_properties(obs-fork-core-regressions PROPERTIES FOLDER "tests")

if(OS_WINDOWS AND TARGET obs-browser)
  add_executable(obs-fork-browser-texture-regressions browser-texture-regressions.cpp)
  target_include_directories(
    obs-fork-browser-texture-regressions
    PRIVATE "${CMAKE_SOURCE_DIR}/libobs" "${CMAKE_SOURCE_DIR}/plugins/obs-browser"
  )
  target_compile_definitions(obs-fork-browser-texture-regressions PRIVATE LIBOBS_EXPORTS)
  set_target_properties(obs-fork-browser-texture-regressions PROPERTIES FOLDER "tests")
  add_test(NAME obs-fork-browser-texture-regressions COMMAND obs-fork-browser-texture-regressions)

  add_executable(obs-fork-browser-texture-gpu-regressions browser-texture-gpu-regressions.cpp)
  target_include_directories(
    obs-fork-browser-texture-gpu-regressions
    PRIVATE "${CMAKE_SOURCE_DIR}/plugins/obs-browser"
  )
  target_link_libraries(obs-fork-browser-texture-gpu-regressions PRIVATE OBS::libobs d3d11 dxgi)
  add_dependencies(obs-fork-browser-texture-gpu-regressions libobs-d3d11)
  set_target_properties(obs-fork-browser-texture-gpu-regressions PROPERTIES FOLDER "tests")
  add_test(
    NAME obs-fork-browser-texture-gpu-regressions
    COMMAND obs-fork-browser-texture-gpu-regressions "$<TARGET_FILE:libobs-d3d11>"
  )
  set_tests_properties(
    obs-fork-browser-texture-gpu-regressions
    PROPERTIES
      TIMEOUT 30
      ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${CMAKE_BINARY_DIR}/rundir/$<CONFIG>/bin/64bit"
  )
endif()

find_package(Qt6 REQUIRED Core)
add_executable(
  obs-fork-telemetry-regressions
  telemetry-regressions.cpp
  "${CMAKE_SOURCE_DIR}/frontend/utility/NativeTelemetryGuard.cpp"
)
target_include_directories(obs-fork-telemetry-regressions PRIVATE "${CMAKE_SOURCE_DIR}/frontend/utility")
set_target_properties(obs-fork-telemetry-regressions PROPERTIES FOLDER "tests")
add_test(NAME obs-fork-telemetry-regressions COMMAND obs-fork-telemetry-regressions)

add_executable(
  obs-fork-plugin-regressions
  plugin-regressions.cpp
  "${CMAKE_SOURCE_DIR}/shared/plugin-path-fingerprint/PluginPathFingerprint.cpp"
)
target_include_directories(
  obs-fork-plugin-regressions
  PRIVATE
    "${CMAKE_SOURCE_DIR}/plugins/obs-vst3"
    "${CMAKE_SOURCE_DIR}/shared/plugin-path-fingerprint"
)
target_link_libraries(obs-fork-plugin-regressions PRIVATE Qt::Core)
set_target_properties(obs-fork-plugin-regressions PROPERTIES FOLDER "tests")

add_test(
  NAME obs-fork-source-contracts
  COMMAND
    "${CMAKE_COMMAND}" -DOBS_SOURCE_DIR=${CMAKE_SOURCE_DIR} -P
    "${CMAKE_CURRENT_SOURCE_DIR}/source-contracts.cmake"
)
add_test(NAME obs-fork-core-regressions COMMAND obs-fork-core-regressions)
add_test(NAME obs-fork-plugin-regressions COMMAND obs-fork-plugin-regressions)
set_tests_properties(
  obs-fork-plugin-regressions
  PROPERTIES ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
)

if(OS_WINDOWS AND TARGET obs-vst3-hosting)
  add_library(
    obs-fork-vst3-factory-only MODULE
    vst3-factory-only.cpp
    "${VST3SDK_PATH}/pluginterfaces/base/funknown.cpp"
    "${VST3SDK_PATH}/pluginterfaces/base/coreiids.cpp"
  )
  target_link_libraries(obs-fork-vst3-factory-only PRIVATE VST3::SDK)
  set_target_properties(obs-fork-vst3-factory-only PROPERTIES FOLDER "tests" PREFIX "" SUFFIX ".vst3")
  add_executable(obs-fork-vst3-scan-regressions vst3-scan-regressions.cpp)
  target_link_libraries(obs-fork-vst3-scan-regressions PRIVATE obs-vst3-hosting Qt::Core)
  add_dependencies(obs-fork-vst3-scan-regressions obs-vst3-scanner obs-fork-vst3-factory-only)
  set_target_properties(obs-fork-vst3-scan-regressions PROPERTIES FOLDER "tests")
  add_test(
    NAME obs-fork-vst3-scan-regressions
    COMMAND obs-fork-vst3-scan-regressions "$<TARGET_FILE:obs-vst3-scanner>" "$<TARGET_FILE:obs-fork-vst3-factory-only>"
  )
  set_tests_properties(
    obs-fork-vst3-scan-regressions
    PROPERTIES
      TIMEOUT 55
      ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:${CMAKE_BINARY_DIR}/rundir/$<CONFIG>/bin/64bit;PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
  )
endif()
