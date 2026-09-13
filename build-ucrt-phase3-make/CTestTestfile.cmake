# CMake generated Testfile for 
# Source directory: C:/Users/rusel_5mliqzd/Downloads/AnitoPlume-master/anitoplumev2
# Build directory: C:/Users/rusel_5mliqzd/Downloads/AnitoPlume-master/anitoplumev2/build-ucrt-phase3-make
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test("foundation_startup" "C:/Users/rusel_5mliqzd/Downloads/AnitoPlume-master/anitoplumev2/build-ucrt-phase3-make/anitoplumev2.exe" "--single-frame")
set_tests_properties("foundation_startup" PROPERTIES  WORKING_DIRECTORY "C:/Users/rusel_5mliqzd/Downloads/AnitoPlume-master/anitoplumev2" _BACKTRACE_TRIPLES "C:/Users/rusel_5mliqzd/Downloads/AnitoPlume-master/anitoplumev2/CMakeLists.txt;155;add_test;C:/Users/rusel_5mliqzd/Downloads/AnitoPlume-master/anitoplumev2/CMakeLists.txt;0;")
add_test("terrain_loader" "C:/Users/rusel_5mliqzd/Downloads/AnitoPlume-master/anitoplumev2/build-ucrt-phase3-make/terrain_loader_test.exe" "C:/Users/rusel_5mliqzd/Downloads/AnitoPlume-master/anitoplumev2/src/tests/fixtures/triangle.obj")
set_tests_properties("terrain_loader" PROPERTIES  WORKING_DIRECTORY "C:/Users/rusel_5mliqzd/Downloads/AnitoPlume-master/anitoplumev2" _BACKTRACE_TRIPLES "C:/Users/rusel_5mliqzd/Downloads/AnitoPlume-master/anitoplumev2/CMakeLists.txt;174;add_test;C:/Users/rusel_5mliqzd/Downloads/AnitoPlume-master/anitoplumev2/CMakeLists.txt;0;")
add_test("plume_simulation" "C:/Users/rusel_5mliqzd/Downloads/AnitoPlume-master/anitoplumev2/build-ucrt-phase3-make/plume_simulation_test.exe")
set_tests_properties("plume_simulation" PROPERTIES  _BACKTRACE_TRIPLES "C:/Users/rusel_5mliqzd/Downloads/AnitoPlume-master/anitoplumev2/CMakeLists.txt;194;add_test;C:/Users/rusel_5mliqzd/Downloads/AnitoPlume-master/anitoplumev2/CMakeLists.txt;0;")
