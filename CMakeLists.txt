cmake_minimum_required(VERSION 3.16)

project(jetcam_stack VERSION 0.4.0 LANGUAGES CXX)

include(CTest)
add_subdirectory(app/jetcam-raw-test)
add_subdirectory(app/jetcamd)
