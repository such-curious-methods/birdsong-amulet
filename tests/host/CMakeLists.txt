cmake_minimum_required(VERSION 3.16)

project(amulet_host_tests LANGUAGES C)

enable_testing()

add_executable(
    test_ble_classifier
    test_ble_classifier.c
    ../../main/amulet_ble_classifier.c
)
target_include_directories(test_ble_classifier PRIVATE ../../main)
target_compile_features(test_ble_classifier PRIVATE c_std_11)
target_compile_options(
    test_ble_classifier
    PRIVATE -Wall -Wextra -Werror -Wpedantic
)

add_test(NAME ble_classifier COMMAND test_ble_classifier)

add_executable(
    test_ambient_observer
    test_ambient_observer.c
    ../../main/amulet_ambient_observer.c
)
target_include_directories(test_ambient_observer PRIVATE ../../main)
target_compile_features(test_ambient_observer PRIVATE c_std_11)
target_compile_options(
    test_ambient_observer
    PRIVATE -Wall -Wextra -Werror -Wpedantic
)

add_test(NAME ambient_observer COMMAND test_ambient_observer)

add_executable(
    test_signal_gate
    test_signal_gate.c
    ../../main/amulet_signal_gate.c
)
target_include_directories(test_signal_gate PRIVATE ../../main)
target_compile_features(test_signal_gate PRIVATE c_std_11)
target_compile_options(
    test_signal_gate
    PRIVATE -Wall -Wextra -Werror -Wpedantic
)

add_test(NAME signal_gate COMMAND test_signal_gate)

add_executable(
    test_hunt
    test_hunt.c
    ../../main/amulet_hunt.c
)
target_include_directories(test_hunt PRIVATE ../../main)
target_compile_features(test_hunt PRIVATE c_std_11)
target_compile_options(
    test_hunt
    PRIVATE -Wall -Wextra -Werror -Wpedantic
)

add_test(NAME hunt COMMAND test_hunt)
