# Host unit tests. Added from engine/CMakeLists.txt (host builds only) and linked against pocketcraft_core.
# test_game.cpp provides a fake createVulkanRenderer() so Game can run on the host.
add_executable(pocketcraft_tests
    test_main.cpp
    test_math.cpp
    test_raycast.cpp
    test_player.cpp
    test_world.cpp
    test_game.cpp)
target_link_libraries(pocketcraft_tests PRIVATE pocketcraft_core)

# enable_testing() here (not in engine/) so the tests run with: ctest --test-dir <build>/tests
enable_testing()
add_test(NAME pocketcraft_tests COMMAND pocketcraft_tests)
