# One ctest per behaviour test in tests/oracle_tests.py.
# SPDX-License-Identifier: AGPL-3.0-only
find_package(Python3 3.10 COMPONENTS Interpreter REQUIRED)

set(JUCE_ORACLE_TESTS
    b1_runner_reports_juce_9_0_4
    b1_lists_supported_kinds
    b1_cli_usage_errors_exit_2
    b2_value_tree_listener_fixtures_run
    b2_selftest_writes_valid_wav
    b2_manifest_hashes_match_outputs
    b2_scenario_set_hash_matches_reference
    b2_outputs_are_deterministic
    b2_unsupported_kind_is_reported
    b2_malformed_scenario_is_an_error
    b2_refuses_non_empty_output_dir
    b2_syrp_scenarios_run
    b3_workflow_lint
    b3_release_dry_run)

file(TO_CMAKE_PATH "${JUCE_ORACLE_SCENARIOS_DIR}" _oracle_scenarios_dir)

foreach(test_name IN LISTS JUCE_ORACLE_TESTS)
    add_test(NAME ${test_name}
        COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/tests/oracle_tests.py"
                ${test_name} $<TARGET_FILE:juce-oracle>
                "${CMAKE_CURRENT_BINARY_DIR}/test-work/${test_name}"
                "${_oracle_scenarios_dir}")
    set_tests_properties(${test_name} PROPERTIES SKIP_RETURN_CODE 77 LABELS "${test_name}")
endforeach()
