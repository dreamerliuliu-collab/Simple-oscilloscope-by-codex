@echo off
setlocal
set "SCRIPT_DIR=%~dp0"
set "BUNDLED_PYTHON=%USERPROFILE%\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe"

if exist "%BUNDLED_PYTHON%" (
    "%BUNDLED_PYTHON%" "%SCRIPT_DIR%scope_mcu_simulator.py" %*
    goto :eof
)

where python >nul 2>nul
if %errorlevel%==0 (
    python "%SCRIPT_DIR%scope_mcu_simulator.py" %*
    goto :eof
)

echo Python runtime not found.
exit /b 1
