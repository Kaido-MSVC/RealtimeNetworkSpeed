@echo off
where pwsh >nul 2>nul
if errorlevel 1 (
 powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build-and-test.ps1"
) else (
 pwsh -NoProfile -ExecutionPolicy Bypass -File "%~dp0build-and-test.ps1"
)
exit /b %errorlevel%
