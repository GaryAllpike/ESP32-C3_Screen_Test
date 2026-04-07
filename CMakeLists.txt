# Boilerplate order required by ESP-IDF (see examples/get-started/hello_world).
cmake_minimum_required(VERSION 3.22)

# If COMPONENTS was ever passed via -D or CMake GUI, it stays in the cache and forces
# "Minimal build - OFF" (ESP-IDF disregards MINIMAL_BUILD when COMPONENTS is set).
unset(COMPONENTS CACHE)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# Only `main` plus transitive REQUIRES/PRIV_REQUIRES — not the full IDF component tree.
idf_build_set_property(MINIMAL_BUILD ON)
project(esp32-c3-screen-test)
