@echo off
setlocal
if not defined QT_DIR set "QT_DIR=D:\qt\6.12.0\msvc2022_64"
set "PATH=%QT_DIR%\bin;%PATH%"
start "" "%~dp0build\Release\RealtimeNetworkSpeed.exe"
