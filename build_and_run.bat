@echo off

:: ============================================================================
:: VORTEX ENGINE - ULTRA PERFORMANCE BUILD & RUN SCRIPT
:: ============================================================================

cd /d "%~dp0"

echo [1/3] Initializing Visual Studio build environment...

where cl >nul 2>nul
if errorlevel 1 (
    call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul
    call "C:\Program Files\Microsoft Visual Studio\2026\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul
)

where cl >nul 2>nul
if errorlevel 1 (
    echo [ERROR] Visual Studio compiler cl.exe was not found.
    pause
    exit /b 1
)

echo.
echo [2/3] Compiling VortexEngine (C++23 / Ultra-Performance)...
echo ----------------------------------------------------------------------------

:: Compile from root, output executable to the engine's subfolder
cl /O2 /std:c++latest /arch:AVX2 /EHsc /D _WIN32_WINNT=0x0A00 /I "engine\VortexEngine" "engine\VortexEngine\VortexEngine.cpp" /Fe:"engine\VortexEngine\VortexEngine.exe"

if errorlevel 1 (
    echo.
    echo [ERROR] Compilation failed. Aborting execution.
    pause
    exit /b 1
)

echo.
echo ----------------------------------------------------------------------------
echo [SUCCESS] Engine compiled successfully.
echo [3/3] Launching VortexEngine and Python Analytics Reader...
echo ----------------------------------------------------------------------------
echo.

del *.obj >nul 2>nul

:: Navigate into the engine directory so relative paths (like config.json) resolve natively
cd engine\VortexEngine

:: Run C++ Engine and pipe to Python (referencing relative path to analytics)
VortexEngine.exe | py -3.14t -u ..\..\analytics\reader_engine.py

echo.
echo ============================================================================
echo Process finished.
pause
