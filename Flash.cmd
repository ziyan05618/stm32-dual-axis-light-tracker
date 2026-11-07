@echo off
setlocal
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0flash.ps1"
set "FLASH_EXIT=%ERRORLEVEL%"
echo.
if not "%FLASH_EXIT%"=="0" echo Flash failed. See the error above and the evidence folder.
pause
exit /b %FLASH_EXIT%
