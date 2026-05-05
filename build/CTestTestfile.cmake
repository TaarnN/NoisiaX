# CMake generated Testfile for 
# Source directory: /workspace
# Build directory: /workspace/build
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test([=[noisiax_tests]=] "/workspace/build/noisiax_tests")
set_tests_properties([=[noisiax_tests]=] PROPERTIES  _BACKTRACE_TRIPLES "/workspace/CMakeLists.txt;130;add_test;/workspace/CMakeLists.txt;0;")
add_test([=[noisiax_cli_validate]=] "/workspace/build/noisiax" "validate" "/workspace/build/scenarios/happy_path.yaml")
set_tests_properties([=[noisiax_cli_validate]=] PROPERTIES  _BACKTRACE_TRIPLES "/workspace/CMakeLists.txt;131;add_test;/workspace/CMakeLists.txt;0;")
add_test([=[noisiax_cli_compile]=] "/workspace/build/noisiax" "compile" "/workspace/build/scenarios/happy_path.yaml")
set_tests_properties([=[noisiax_cli_compile]=] PROPERTIES  _BACKTRACE_TRIPLES "/workspace/CMakeLists.txt;135;add_test;/workspace/CMakeLists.txt;0;")
add_test([=[noisiax_cli_run]=] "/workspace/build/noisiax" "run" "/workspace/build/scenarios/happy_path.yaml")
set_tests_properties([=[noisiax_cli_run]=] PROPERTIES  _BACKTRACE_TRIPLES "/workspace/CMakeLists.txt;139;add_test;/workspace/CMakeLists.txt;0;")
subdirs("_deps/yaml-cpp-build")
