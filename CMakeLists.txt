cmake_minimum_required(VERSION 3.27)

project(confined-floats)

set(CMAKE_CXX_STANDARD 23)

add_library(confined-floats SHARED src/main.cpp)

find_package(PkgConfig REQUIRED)
pkg_check_modules(deps REQUIRED IMPORTED_TARGET hyprland)
target_link_libraries(confined-floats PRIVATE rt PkgConfig::deps)

install(TARGETS confined-floats)
