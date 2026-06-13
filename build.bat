@echo off
setlocal

echo ============================================================
echo  Vocal Doubler -- VST3 Build Script
echo ============================================================
echo.

:: Check for cmake
where cmake >nul 2>&1
if %errorlevel% neq 0 (
    echo ERROR: cmake not found. Install CMake from https://cmake.org/download/
    echo        and ensure it is on your PATH.
    pause
    exit /b 1
)

:: Configure (first run downloads JUCE -- may take a few minutes)
echo [1/2] Configuring with CMake...
cmake -B build -G "Visual Studio 16 2019" -A x64
if %errorlevel% neq 0 (
    echo.
    echo ERROR: CMake configuration failed.
    pause
    exit /b 1
)

:: Build Release
echo.
echo [2/2] Building Release...
cmake --build build --config Release --parallel
if %errorlevel% neq 0 (
    echo.
    echo ERROR: Build failed. Check the output above for details.
    pause
    exit /b 1
)

echo.
echo ============================================================
echo  Build complete!
echo  VST3 installed to:
echo    %%CommonProgramFiles%%\VST3\Vocal Doubler.vst3
echo ============================================================
pause
