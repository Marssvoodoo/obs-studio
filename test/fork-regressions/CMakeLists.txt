add_executable(obs-fork-core-regressions EXCLUDE_FROM_ALL core-regressions.c)
target_include_directories(obs-fork-core-regressions PRIVATE "${CMAKE_SOURCE_DIR}/libobs")
set_target_properties(obs-fork-core-regressions PROPERTIES FOLDER "tests")

add_test(
  NAME obs-fork-source-contracts
  COMMAND
    "${CMAKE_COMMAND}" -DOBS_SOURCE_DIR=${CMAKE_SOURCE_DIR} -P
    "${CMAKE_CURRENT_SOURCE_DIR}/source-contracts.cmake"
)
add_test(NAME obs-fork-core-regressions COMMAND obs-fork-core-regressions)
