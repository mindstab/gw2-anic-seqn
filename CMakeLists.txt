cmake_minimum_required(VERSION 3.20)
cmake_policy(SET CMP0091 NEW)

project(gw2-anic-seqn LANGUAGES CXX)

include(FetchContent) 

FetchContent_Declare(
    gw2-pf
    GIT_REPOSITORY https://github.com/mindstab/gw2-pf.git
    GIT_TAG        a41cba261ad76a8729ead91798961eca71e1df02
)
FetchContent_MakeAvailable(gw2-pf)

add_executable(${PROJECT_NAME} main.cpp)
target_link_libraries(${PROJECT_NAME} PRIVATE gw2::pf)
target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23)

set_target_properties(${PROJECT_NAME} PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")