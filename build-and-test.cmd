@echo off
where pwsh >nul 2>nul
if errorlevel 1 (
 powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build-and-test.ps1" -Pause %*
) else (
 pwsh -NoProfile -ExecutionPolicy Bypass -File "%~dp0build-and-test.ps1" -Pause %*
)
exit /b %errorlevel%
