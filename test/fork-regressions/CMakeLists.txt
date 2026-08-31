add_executable(obs-fork-core-regressions core-regressions.c)
target_include_directories(obs-fork-core-regressions PRIVATE "${CMAKE_SOURCE_DIR}/libobs")
set_target_properties(obs-fork-core-regressions PROPERTIES FOLDER "tests")

find_package(Qt6 REQUIRED Core)
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
