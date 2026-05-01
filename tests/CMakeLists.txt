set(MDMTSP_TEST_COMMON_LINK_LIBS
    mdmtsp_core
    mdmtsp_tsp
    mdmtsp_project_options
    mdmtsp_project_warnings
)

add_executable(test_instance
    test_instance.cpp
)
target_link_libraries(test_instance PRIVATE ${MDMTSP_TEST_COMMON_LINK_LIBS})
add_test(NAME test_instance COMMAND test_instance)

add_executable(test_objective
    test_objective.cpp
)
target_link_libraries(test_objective PRIVATE ${MDMTSP_TEST_COMMON_LINK_LIBS})
add_test(NAME test_objective COMMAND test_objective)

add_executable(test_validator
    test_validator.cpp
)
target_link_libraries(test_validator PRIVATE ${MDMTSP_TEST_COMMON_LINK_LIBS})
add_test(NAME test_validator COMMAND test_validator)

add_executable(test_tsp_operators
    test_tsp_operators.cpp
)
target_link_libraries(test_tsp_operators PRIVATE ${MDMTSP_TEST_COMMON_LINK_LIBS})
add_test(NAME test_tsp_operators COMMAND test_tsp_operators)

add_executable(test_mdmtsp_small_cases
    test_mdmtsp_small_cases.cpp
)
target_link_libraries(test_mdmtsp_small_cases PRIVATE ${MDMTSP_TEST_COMMON_LINK_LIBS})
add_test(NAME test_mdmtsp_small_cases COMMAND test_mdmtsp_small_cases)