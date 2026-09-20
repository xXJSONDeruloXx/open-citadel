cmake_minimum_required(VERSION 3.22.1)
project(OpenCitadelQuest LANGUAGES CXX)

find_package(OpenXR REQUIRED CONFIG)

add_library(open-citadel-quest SHARED quest_probe.cpp)
target_compile_features(open-citadel-quest PRIVATE cxx_std_20)
target_compile_definitions(open-citadel-quest PRIVATE XR_USE_PLATFORM_ANDROID)
target_link_libraries(open-citadel-quest PRIVATE
    OpenXR::openxr_loader
    android
    log
)
