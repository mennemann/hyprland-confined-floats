cmake_minimum_required(VERSION 3.27)

project(confined-floats)

set(CMAKE_CXX_STANDARD 23)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(confined-floats SHARED src/main.cpp)

find_package(PkgConfig REQUIRED)
pkg_check_modules(deps REQUIRED IMPORTED_TARGET hyprland)
pkg_check_modules(AQUAMARINE REQUIRED aquamarine)
pkg_check_modules(HYPRCURSOR REQUIRED hyprcursor)
pkg_check_modules(HYPRGRAPHICS REQUIRED hyprgraphics)
pkg_check_modules(HYPRLANG REQUIRED hyprlang)
pkg_check_modules(HYPRUTILS REQUIRED hyprutils)

target_compile_definitions(confined-floats PRIVATE
    CONFINED_FLOATS_AQUAMARINE_VERSION="${AQUAMARINE_VERSION}"
    CONFINED_FLOATS_HYPRCURSOR_VERSION="${HYPRCURSOR_VERSION}"
    CONFINED_FLOATS_HYPRGRAPHICS_VERSION="${HYPRGRAPHICS_VERSION}"
    CONFINED_FLOATS_HYPRLANG_VERSION="${HYPRLANG_VERSION}"
    CONFINED_FLOATS_HYPRUTILS_VERSION="${HYPRUTILS_VERSION}"
)
target_link_libraries(confined-floats PRIVATE rt PkgConfig::deps)

install(TARGETS confined-floats)
