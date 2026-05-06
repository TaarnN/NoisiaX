# CMake generated Testfile for 
# Source directory: /Users/taarn/Documents/PGM/Prog-Languages/NoisiaX
# Build directory: /Users/taarn/Documents/PGM/Prog-Languages/NoisiaX/build
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test([=[noisiax_tests]=] "/Users/taarn/Documents/PGM/Prog-Languages/NoisiaX/build/noisiax_tests")
set_tests_properties([=[noisiax_tests]=] PROPERTIES  _BACKTRACE_TRIPLES "/Users/taarn/Documents/PGM/Prog-Languages/NoisiaX/CMakeLists.txt;135;add_test;/Users/taarn/Documents/PGM/Prog-Languages/NoisiaX/CMakeLists.txt;0;")
add_test([=[noisiax_cli_validate]=] "/Users/taarn/Documents/PGM/Prog-Languages/NoisiaX/build/noisiax" "validate" "/Users/taarn/Documents/PGM/Prog-Languages/NoisiaX/build/scenarios/happy_path.yaml")
set_tests_properties([=[noisiax_cli_validate]=] PROPERTIES  _BACKTRACE_TRIPLES "/Users/taarn/Documents/PGM/Prog-Languages/NoisiaX/CMakeLists.txt;136;add_test;/Users/taarn/Documents/PGM/Prog-Languages/NoisiaX/CMakeLists.txt;0;")
add_test([=[noisiax_cli_compile]=] "/Users/taarn/Documents/PGM/Prog-Languages/NoisiaX/build/noisiax" "compile" "/Users/taarn/Documents/PGM/Prog-Languages/NoisiaX/build/scenarios/happy_path.yaml")
set_tests_properties([=[noisiax_cli_compile]=] PROPERTIES  _BACKTRACE_TRIPLES "/Users/taarn/Documents/PGM/Prog-Languages/NoisiaX/CMakeLists.txt;140;add_test;/Users/taarn/Documents/PGM/Prog-Languages/NoisiaX/CMakeLists.txt;0;")
add_test([=[noisiax_cli_run]=] "/Users/taarn/Documents/PGM/Prog-Languages/NoisiaX/build/noisiax" "run" "/Users/taarn/Documents/PGM/Prog-Languages/NoisiaX/build/scenarios/happy_path.yaml")
set_tests_properties([=[noisiax_cli_run]=] PROPERTIES  _BACKTRACE_TRIPLES "/Users/taarn/Documents/PGM/Prog-Languages/NoisiaX/CMakeLists.txt;144;add_test;/Users/taarn/Documents/PGM/Prog-Languages/NoisiaX/CMakeLists.txt;0;")
add_test([=[noisiax_cli_resolve]=] "/Users/taarn/Documents/PGM/Prog-Languages/NoisiaX/build/noisiax" "resolve" "/Users/taarn/Documents/PGM/Prog-Languages/NoisiaX/build/scenarios/v4_composition_base.yaml" "--output" "/Users/taarn/Documents/PGM/Prog-Languages/NoisiaX/build/resolved_v4_composition.yaml")
set_tests_properties([=[noisiax_cli_resolve]=] PROPERTIES  _BACKTRACE_TRIPLES "/Users/taarn/Documents/PGM/Prog-Languages/NoisiaX/CMakeLists.txt;148;add_test;/Users/taarn/Documents/PGM/Prog-Languages/NoisiaX/CMakeLists.txt;0;")
add_test([=[noisiax_cli_experiment]=] "/Users/taarn/Documents/PGM/Prog-Languages/NoisiaX/build/noisiax" "experiment" "/Users/taarn/Documents/PGM/Prog-Languages/NoisiaX/build/scenarios/v4_experiment_basic.yaml" "--output-dir" "/Users/taarn/Documents/PGM/Prog-Languages/NoisiaX/build/v4_experiment_output")
set_tests_properties([=[noisiax_cli_experiment]=] PROPERTIES  _BACKTRACE_TRIPLES "/Users/taarn/Documents/PGM/Prog-Languages/NoisiaX/CMakeLists.txt;153;add_test;/Users/taarn/Documents/PGM/Prog-Languages/NoisiaX/CMakeLists.txt;0;")
subdirs("_deps/yaml-cpp-build")
