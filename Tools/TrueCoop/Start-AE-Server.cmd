@echo off
pushd "%~dp0"
if errorlevel 1 exit /b 1
if not exist "SkyrimTogetherServer.exe" (
    echo SkyrimTogetherServer.exe is missing. Install the full AE package first.
    popd
    pause
    exit /b 1
)
"SkyrimTogetherServer.exe"
set "result=%errorlevel%"
popd
if not "%result%"=="0" pause
exit /b %result%
