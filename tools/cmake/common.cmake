# Copyright 2023 jacqueline <me@jacqueline.id.au>
#
# SPDX-License-Identifier: CC0-1.0

# For more information about build system see
# https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html

set(PROJECT_VER "2.1.0")

# esp-idf sets the C++ standard weird. Set cmake vars to match.
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_EXTENSIONS ON)

# Build only the subset of components that we actually depend on.
set(COMPONENTS "")

# External dependencies
list(APPEND EXTRA_COMPONENT_DIRS "$ENV{PROJ_PATH}/lib/alac")
list(APPEND EXTRA_COMPONENT_DIRS "$ENV{PROJ_PATH}/lib/catch2")
list(APPEND EXTRA_COMPONENT_DIRS "$ENV{PROJ_PATH}/lib/cbor")
list(APPEND EXTRA_COMPONENT_DIRS "$ENV{PROJ_PATH}/lib/esp-idf-lua")
list(APPEND EXTRA_COMPONENT_DIRS "$ENV{PROJ_PATH}/lib/esp_littlefs")
list(APPEND EXTRA_COMPONENT_DIRS "$ENV{PROJ_PATH}/lib/komihash")
list(APPEND EXTRA_COMPONENT_DIRS "$ENV{PROJ_PATH}/lib/libcppbor")
list(APPEND EXTRA_COMPONENT_DIRS "$ENV{PROJ_PATH}/lib/libmad")
list(APPEND EXTRA_COMPONENT_DIRS "$ENV{PROJ_PATH}/lib/libtags")
list(APPEND EXTRA_COMPONENT_DIRS "$ENV{PROJ_PATH}/lib/lua-linenoise")
list(APPEND EXTRA_COMPONENT_DIRS "$ENV{PROJ_PATH}/lib/lua-term")
list(APPEND EXTRA_COMPONENT_DIRS "$ENV{PROJ_PATH}/lib/luavgl")
list(APPEND EXTRA_COMPONENT_DIRS "$ENV{PROJ_PATH}/lib/lvgl")
list(APPEND EXTRA_COMPONENT_DIRS "$ENV{PROJ_PATH}/lib/millershuffle")
list(APPEND EXTRA_COMPONENT_DIRS "$ENV{PROJ_PATH}/lib/oled_sh1106")
list(APPEND EXTRA_COMPONENT_DIRS "$ENV{PROJ_PATH}/lib/drflac")
list(APPEND EXTRA_COMPONENT_DIRS "$ENV{PROJ_PATH}/lib/ogg")
list(APPEND EXTRA_COMPONENT_DIRS "$ENV{PROJ_PATH}/lib/opusfile")
list(APPEND EXTRA_COMPONENT_DIRS "$ENV{PROJ_PATH}/lib/result")
list(APPEND EXTRA_COMPONENT_DIRS "$ENV{PROJ_PATH}/lib/speexdsp")
list(APPEND EXTRA_COMPONENT_DIRS "$ENV{PROJ_PATH}/lib/tinyfsm")
list(APPEND EXTRA_COMPONENT_DIRS "$ENV{PROJ_PATH}/lib/tremor")
list(APPEND EXTRA_COMPONENT_DIRS "$ENV{PROJ_PATH}/lib/wavpack")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Additional warnings used when compiling our components.
# Unable to be used due to issues in ESP-IDF includes are: -Wpedantic
# -Wuseless-cast -Wconversion -Wold-style-cast -Wsign-conversion -Wcast-align
#
# These flags are split by language because some are C++-only and would
# break C compilation (e.g. -Wnon-virtual-dtor, -Woverloaded-virtual,
# -Wno-deprecated-enum-enum-conversion). They are wrapped in generator
# expressions so they only apply to the matching source language.
set(_EXTRA_WARNINGS_COMMON
  "-Wunused" "-Wmisleading-indentation" "-Wduplicated-cond"
  "-Wduplicated-branches" "-Wlogical-op" "-Wnull-dereference"
  "-Wdouble-promotion" "-Wformat=2" "-Wimplicit-fallthrough"
  "-Wno-array-bounds" "-Wno-missing-field-initializers"
  # Relaxations for newer GCC / C++23 toolchain (esp-idf 6.x)
  "-Wno-error=cpp" "-Wno-error=null-dereference"
  "-Wno-error=deprecated-declarations")
set(_EXTRA_WARNINGS_CXX_ONLY
  "-Wnon-virtual-dtor" "-Woverloaded-virtual"
  "-Wno-deprecated-enum-enum-conversion"
  "-Wno-error=deprecated")

set(EXTRA_WARNINGS "")
foreach(_flag IN LISTS _EXTRA_WARNINGS_COMMON)
  list(APPEND EXTRA_WARNINGS "$<$<COMPILE_LANGUAGE:C,CXX>:${_flag}>")
endforeach()
foreach(_flag IN LISTS _EXTRA_WARNINGS_CXX_ONLY)
  list(APPEND EXTRA_WARNINGS "$<$<COMPILE_LANGUAGE:CXX>:${_flag}>")
endforeach()

# Extra build flags that should apply to the entire build. This should mostly
# just be used to setting flags that our external dependencies requires.
# Otherwise, prefer adding per-component build flags to keep things neat.
idf_build_set_property(COMPILE_OPTIONS "-DLV_CONF_INCLUDE_SIMPLE" APPEND)
idf_build_set_property(COMPILE_OPTIONS "-DTCB_SPAN_NAMESPACE_NAME=cpp" APPEND)
