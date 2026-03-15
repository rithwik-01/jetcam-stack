if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_executable(jetcam-raw-test
        src/main.cpp
    )

    target_compile_features(jetcam-raw-test PRIVATE cxx_std_17)
    target_include_directories(jetcam-raw-test PRIVATE include)
    target_compile_options(jetcam-raw-test PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wshadow
    )

    install(TARGETS jetcam-raw-test RUNTIME DESTINATION bin)
else()
    message(STATUS "Skipping jetcam-raw-test: the V4L2 executable requires Linux")
endif()

if(BUILD_TESTING)
    add_executable(jetcam-raw-test-stats-test tests/stats_test.cpp)
    target_compile_features(jetcam-raw-test-stats-test PRIVATE cxx_std_17)
    target_include_directories(jetcam-raw-test-stats-test PRIVATE include)
    target_compile_options(jetcam-raw-test-stats-test PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wshadow
    )
    add_test(NAME jetcam-raw-test-stats COMMAND jetcam-raw-test-stats-test)
    if(TARGET jetcam-raw-test)
        add_test(NAME jetcam-raw-test-help COMMAND jetcam-raw-test --help)
        add_test(NAME jetcam-raw-test-invalid-option
                 COMMAND jetcam-raw-test --definitely-invalid)
        set_tests_properties(jetcam-raw-test-invalid-option PROPERTIES WILL_FAIL TRUE)
    endif()
endif()
