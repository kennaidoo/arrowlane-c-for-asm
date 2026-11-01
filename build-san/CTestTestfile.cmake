# CMake generated Testfile for 
# Source directory: /src
# Build directory: /src/build-san
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test([=[aln_test]=] "/src/build-san/aln_test")
set_tests_properties([=[aln_test]=] PROPERTIES  _BACKTRACE_TRIPLES "/src/CMakeLists.txt;47;add_test;/src/CMakeLists.txt;0;")
add_test([=[float_test]=] "/src/build-san/float_test")
set_tests_properties([=[float_test]=] PROPERTIES  _BACKTRACE_TRIPLES "/src/CMakeLists.txt;48;add_test;/src/CMakeLists.txt;0;")
add_test([=[fuzz_frame]=] "/src/build-san/fuzz_frame")
set_tests_properties([=[fuzz_frame]=] PROPERTIES  _BACKTRACE_TRIPLES "/src/CMakeLists.txt;53;add_test;/src/CMakeLists.txt;0;")
add_test([=[cli_bench]=] "/src/build-san/arrowlane-c-for-asm" "bench" "--rows" "1000" "--repeats" "3")
set_tests_properties([=[cli_bench]=] PROPERTIES  _BACKTRACE_TRIPLES "/src/CMakeLists.txt;54;add_test;/src/CMakeLists.txt;0;")
add_test([=[cli_describe]=] "/src/build-san/arrowlane-c-for-asm" "describe")
set_tests_properties([=[cli_describe]=] PROPERTIES  PASS_REGULAR_EXPRESSION "\"name\":\"arrowlane-c-for-asm\"" _BACKTRACE_TRIPLES "/src/CMakeLists.txt;55;add_test;/src/CMakeLists.txt;0;")
