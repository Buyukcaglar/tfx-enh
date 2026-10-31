# clang through Zig for x86_64-windows-gnu (W01's second compiler family):
#   tools\win\vsenv.cmd cmake -S . -B private/build/clang -G Ninja
#     -DCMAKE_TOOLCHAIN_FILE=tools/win/zig-toolchain.cmake
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR AMD64)
set(CMAKE_C_COMPILER "${CMAKE_CURRENT_LIST_DIR}/zig-cc.cmd")
set(CMAKE_AR "${CMAKE_CURRENT_LIST_DIR}/zig-ar.cmd")
set(CMAKE_RANLIB "${CMAKE_CURRENT_LIST_DIR}/zig-ranlib.cmd")
set(CMAKE_RC_COMPILER "${CMAKE_CURRENT_LIST_DIR}/zig-rc.cmd")
