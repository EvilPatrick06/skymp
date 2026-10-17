# TODO: enable for Emscripten once stabilize
if(BUILD_UNIT_TESTS AND NOT EMSCRIPTEN)

  include(${CMAKE_SOURCE_DIR}/cmake/add_papyrus_library_ck.cmake)
  include(${CMAKE_SOURCE_DIR}/cmake/apply_default_settings.cmake)

  #
  # papyrus_test_files
  #

  add_papyrus_library_ck(
    NAME papyrus_test_files 
    DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/papyrus_test_files
    PREBUILT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/papyrus_test_files/pex
    COMPILER_EXECUTABLE_PATH "${SKYRIM_DIR}/Papyrus compiler/PapyrusCompiler.exe"
    PEX_DIR_VARIABLE papyrus_test_files_pex_dir
  )

  #
  # unit executable
  #

  file(GLOB src "${CMAKE_CURRENT_SOURCE_DIR}/*")
  list(APPEND src "${CMAKE_SOURCE_DIR}/.clang-format")
  if(TARGET platform_lib)
    file(GLOB src_windows "${CMAKE_CURRENT_SOURCE_DIR}/platform_lib_tests/*")
    list(APPEND src ${src_windows})
  endif()

  add_executable(unit ${src})

  find_package(Catch2 CONFIG REQUIRED)
  target_link_libraries(unit PRIVATE Catch2::Catch2)

  target_link_libraries(unit PUBLIC server_guest_lib espm)
  if(TARGET platform_lib)
    target_link_libraries(unit PUBLIC platform_lib)
  endif()
  apply_default_settings(TARGETS unit)
  list(APPEND VCPKG_DEPENDENT unit)

  if(SKYRIM_SE)
    target_compile_definitions(unit PRIVATE SKYRIM_SE=1)
  endif()

  if(BUILD_FRONT)
    target_compile_definitions(unit PRIVATE WITH_UI_FRONT=1)
  endif()

  target_compile_definitions(unit PRIVATE
    TEST_PEX_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/papyrus_test_files/standard_scripts\"
    BUILT_PEX_DIR=\"${papyrus_test_files_pex_dir}\"
    SKYRIM_DIR=\"${SKYRIM_DIR}\"
    UNIT_DATA_DIR=\"${UNIT_DATA_DIR}\"
    DIST_DIR=\"${CMAKE_BINARY_DIR}/dist\"
  )

  if(WIN32)
    target_compile_options(unit PRIVATE "/bigobj")
    target_link_libraries(unit PUBLIC Dbghelp.lib)
  endif()

  # The private ledger writer has no game-data dependency. Exercise the
  # actual platform file/rename barriers rather than a mocked acknowledgement.
  add_executable(private-ledger-test "${CMAKE_SOURCE_DIR}/misc/tests/private_ledger_test.cpp")
  target_include_directories(private-ledger-test PRIVATE "${CMAKE_SOURCE_DIR}/skymp5-server/cpp/server_guest_lib")
  apply_default_settings(TARGETS private-ledger-test)
  add_test(NAME private_ledger COMMAND private-ledger-test "${CMAKE_BINARY_DIR}/private-ledger-tests")

  # This contract runner must not use unit/main.cpp, which loads game plugins.
  add_executable(save-capability-test "${CMAKE_SOURCE_DIR}/misc/tests/save_capability_test.cpp")
  target_link_libraries(save-capability-test PRIVATE viet)
  apply_default_settings(TARGETS save-capability-test)
  add_test(NAME save_capability COMMAND save-capability-test)

  add_executable(save-settlement-test "${CMAKE_SOURCE_DIR}/misc/tests/save_settlement_test.cpp")
  target_link_libraries(save-settlement-test PRIVATE viet)
  apply_default_settings(TARGETS save-settlement-test)
  add_test(NAME save_settlement COMMAND save-settlement-test)
  set_tests_properties(save_settlement PROPERTIES TIMEOUT 20)

  add_executable(save-outcome-test "${CMAKE_SOURCE_DIR}/misc/tests/save_outcome_test.cpp")
  target_link_libraries(save-outcome-test PRIVATE viet)
  apply_default_settings(TARGETS save-outcome-test)
  add_test(NAME save_outcome COMMAND save-outcome-test)
  set_tests_properties(save_outcome PROPERTIES TIMEOUT 20)

  add_executable(save-directory-test "${CMAKE_SOURCE_DIR}/misc/tests/save_directory_test.cpp")
  target_include_directories(save-directory-test PRIVATE "${CMAKE_SOURCE_DIR}/skymp5-server/cpp/server_guest_lib")
  apply_default_settings(TARGETS save-directory-test)
  add_test(NAME save_directory COMMAND save-directory-test "${CMAKE_BINARY_DIR}/save-directory-tests")

  # Its own process isolates CWD changes from the unit runner's worker threads.
  add_executable(file-database-path-test "${CMAKE_SOURCE_DIR}/misc/tests/file_database_path_test.cpp")
  target_link_libraries(file-database-path-test PRIVATE server_guest_lib)
  apply_default_settings(TARGETS file-database-path-test)
  add_test(NAME file_database_path COMMAND file-database-path-test "${CMAKE_BINARY_DIR}/file-database-path-tests")

  #
  # ctest tests
  #

  # run without coverage but with dumps
  add_test(
    NAME test_unit
    COMMAND ${CMAKE_COMMAND}
      -DEXE_PATH=$<TARGET_FILE:unit>
      -DCOVERAGE_HTML_OUT_DIR=${CMAKE_BINARY_DIR}/__coverage
      -DCPPCOV=OFF
      -DCPPCOV_PATH=${CPPCOV_PATH}
      -DUNIT_WORKING_DIRECTORY=${CMAKE_BINARY_DIR}
      -P ${CMAKE_SOURCE_DIR}/cmake/run_test_unit.cmake
  )
  set_tests_properties(test_unit PROPERTIES
    FIXTURES_SETUP unit_passed
  )

  # run with coverage but without dumps
  if(CPPCOV_PATH)
    add_test(
      NAME test_unit_coverage
      COMMAND ${CMAKE_COMMAND}
        -DEXE_PATH=$<TARGET_FILE:unit>
        -DCOVERAGE_HTML_OUT_DIR=${CMAKE_BINARY_DIR}/__coverage
        -DCPPCOV=ON
        -DCPPCOV_PATH=${CPPCOV_PATH}
        -P ${CMAKE_SOURCE_DIR}/cmake/run_test_unit.cmake
    )
  endif()
endif()
