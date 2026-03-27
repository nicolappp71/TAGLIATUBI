# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)
set(MY_VERSION "1.1.4")
set(APP_VERSION ${MY_VERSION})
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(MULTI_ORDINI_BLE VERSION ${MY_VERSION})