@echo off
setlocal enabledelayedexpansion
REM Auto-detect rc.exe from the Windows 10 SDK.
REM Searches the SDK bin directory for the newest x64 rc.exe
REM and invokes it with the arguments passed to this script.

set "RC_EXE="
for /d %%d in ("C:\Program Files (x86)\Windows Kits\10\bin\10.*") do (
    if exist "%%d\x64\rc.exe" set "RC_EXE=%%d\x64\rc.exe"
)

if not defined RC_EXE (
    echo ERROR: rc.exe not found in Windows 10 SDK >&2
    exit /b 1
)

"!RC_EXE!" %*
