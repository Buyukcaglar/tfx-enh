@echo off
rem clang through Zig (the second compiler family, W01): zig cc for the
rem x86_64-windows-gnu target. TFX_ZIG overrides the Zig executable.
if not defined TFX_ZIG set "TFX_ZIG=%~dp0..\..\private\tools\zig-x86_64-windows-0.15.2\zig.exe"
"%TFX_ZIG%" cc -target x86_64-windows-gnu %*
