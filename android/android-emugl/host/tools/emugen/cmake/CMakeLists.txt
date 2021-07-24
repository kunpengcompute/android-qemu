cmake_minimum_required(VERSION 3.5)
project(emugen)
get_filename_component(ANDROID_QEMU2_TOP_DIR
                        "${CMAKE_CURRENT_LIST_DIR}/../../../../../../" ABSOLUTE)
get_filename_component(
  ADD_PATH "${ANDROID_QEMU2_TOP_DIR}/android/build/cmake/" ABSOLUTE)
list(APPEND CMAKE_MODULE_PATH "${ADD_PATH}")
include(android)
# Emugen executable (note not unicode safe!)
android_add_executable(
  TARGET emugen
  LICENSE Apache-2.0
  SRC # cmake-format: sortable
      ../ApiGen.cpp ../EntryPoint.cpp ../main.cpp ../Parser.cpp ../strUtils.cpp
      ../TypeFactory.cpp)
target_include_directories(emugen
                           PRIVATE ${ANDROID_QEMU2_TOP_DIR}/android/android-emu)