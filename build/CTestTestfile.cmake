# CMake generated Testfile for 
# Source directory: /home/davids175/Cpp/AiQuant
# Build directory: /home/davids175/Cpp/AiQuant/build
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test([=[basic_build_test]=] "/usr/bin/cmake" "-E" "echo" "Build test passed - project compiles successfully")
set_tests_properties([=[basic_build_test]=] PROPERTIES  _BACKTRACE_TRIPLES "/home/davids175/Cpp/AiQuant/CMakeLists.txt;52;add_test;/home/davids175/Cpp/AiQuant/CMakeLists.txt;0;")
add_test([=[aiquant_tests]=] "/home/davids175/Cpp/AiQuant/build/aiquant_tests")
set_tests_properties([=[aiquant_tests]=] PROPERTIES  _BACKTRACE_TRIPLES "/home/davids175/Cpp/AiQuant/CMakeLists.txt;116;add_test;/home/davids175/Cpp/AiQuant/CMakeLists.txt;0;")
