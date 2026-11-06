@echo off
setlocal
rem Prefer the synchronized release payload, including its deployed dependencies.
if exist "%~dp0dist\RealtimeNetworkSpeed.exe" (
 start "" "%~dp0dist\RealtimeNetworkSpeed.exe"
 exit /b 0
)
if not defined QT_DIR set "QT_DIR=D:\qt\6.12.0\msvc2022_64"
set "PATH=%QT_DIR%\bin;%PATH%"
start "" "%~dp0build\Release\RealtimeNetworkSpeed.exe"
