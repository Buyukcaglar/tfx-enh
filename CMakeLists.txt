# TFX Enhanced, public source: the host's portable code that stands on its
# own (the file formats, the CD image, the OPL2 synthesizer, presentation
# and input helpers) and its test. The game's translated core is not
# published, so the program itself (src/win, mac/) does not build from this
# repository; its Windows build is on the Releases page.
#
#   cmake -S . -B build && cmake --build build && ctest --test-dir build
cmake_minimum_required(VERSION 3.20)
project(tfx_enh C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_library(tfx_portable STATIC
  src/host/deflate.c
  src/host/flac.c
  src/host/zip.c
  src/host/sha1.c
  src/host/sha256.c
  src/host/chd.c
  src/host/cdaudio.c
  src/host/dospath.c
  src/host/scancode.c
  src/host/present.c
  src/audio/opl2.c
)
target_include_directories(tfx_portable PUBLIC src include)
if(NOT MSVC)
  target_link_libraries(tfx_portable PUBLIC m)
endif()

enable_testing()
add_executable(test_pack tests/test_pack.c)
target_link_libraries(test_pack PRIVATE tfx_portable)
add_test(NAME pack COMMAND test_pack)
