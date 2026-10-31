@echo off
if not defined TFX_ZIG set "TFX_ZIG=%~dp0..\..\private\tools\zig-x86_64-windows-0.15.2\zig.exe"
"%TFX_ZIG%" ranlib %*
