@echo off
setlocal EnableDelayedExpansion

rem ============================================================
rem  CIU Android mod - ARM64 Release build
rem  Target : libproject_top.so (arm64-v8a) -> libciu.so
rem
rem  Usage:
rem    build.bat            incremental build
rem    build.bat clean      wipe build dir, reconfigure, build
rem    build.bat push       build then push to /data/local/tmp
rem    build.bat clean push both
rem ============================================================

set "ROOT=%~dp0"
if "%ROOT:~-1%"=="\" set "ROOT=%ROOT:~0,-1%"
set "BUILD_DIR=%ROOT%\build\ARM64-Release"
set "OUT_SO=%BUILD_DIR%\lib\libciu.so"

set "DO_CLEAN="
set "DO_PUSH="
for %%A in (%*) do (
    if /i "%%~A"=="clean" set "DO_CLEAN=1"
    if /i "%%~A"=="push"  set "DO_PUSH=1"
)

echo ============================================================
echo  CIU Android mod  ^|  arm64-v8a  ^|  Release
echo ============================================================
echo.

rem ---------- 1) locate cmake ----------
set "CMAKE_EXE="
set "SDK=%LOCALAPPDATA%\Android\Sdk"

call :try_cmake "%SDK%\cmake\4.0.2\bin\cmake.exe"
call :try_cmake "%SDK%\cmake\3.22.1\bin\cmake.exe"
call :try_cmake "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
call :try_cmake "C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
call :try_cmake "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if not defined CMAKE_EXE call :try_cmake "%ProgramFiles%\CMake\bin\cmake.exe"
if not defined CMAKE_EXE call :try_cmake "%ProgramFiles(x86)%\CMake\bin\cmake.exe"

if not defined CMAKE_EXE (
    for /f "delims=" %%P in ('where cmake 2^>nul') do (
        if not defined CMAKE_EXE set "CMAKE_EXE=%%P"
    )
)
if not defined CMAKE_EXE (
    echo [ERROR] cmake.exe not found.
    echo         Install the Android SDK CMake package, or add cmake to PATH.
    goto :fail
)
echo [1/4] cmake    : %CMAKE_EXE%

rem ---------- 2) locate NDK ----------
rem Prefer the NDK recorded in the existing cache, so a clean rebuild uses
rem exactly the same toolchain as the currently working build.
set "NDK_DIR="
set "CACHED_TOOLCHAIN="
if exist "%BUILD_DIR%\CMakeCache.txt" (
    for /f "tokens=2 delims==" %%L in ('findstr /b /c:"CMAKE_TOOLCHAIN_FILE" "%BUILD_DIR%\CMakeCache.txt" 2^>nul') do (
        if not defined CACHED_TOOLCHAIN set "CACHED_TOOLCHAIN=%%L"
    )
)
if defined CACHED_TOOLCHAIN (
    set "CACHED_NDK=!CACHED_TOOLCHAIN:/build/cmake/android.toolchain.cmake=!"
    set "CACHED_NDK=!CACHED_NDK:\build\cmake\android.toolchain.cmake=!"
    if not "!CACHED_NDK!"=="!CACHED_TOOLCHAIN!" call :try_ndk "!CACHED_NDK!"
)

if defined ANDROID_NDK_HOME call :try_ndk "%ANDROID_NDK_HOME%"
if not defined NDK_DIR if defined ANDROID_NDK_ROOT call :try_ndk "%ANDROID_NDK_ROOT%"
if not defined NDK_DIR if defined NDK_HOME call :try_ndk "%NDK_HOME%"

if not defined NDK_DIR (
    for /f "delims=" %%D in ('dir /b /ad /o-n "%SDK%\ndk" 2^>nul') do (
        if not defined NDK_DIR call :try_ndk "%SDK%\ndk\%%D"
    )
)
if not defined NDK_DIR call :try_ndk "D:\VS2022\Shared\Android\AndroidNDK\android-ndk-r27d"
if not defined NDK_DIR call :try_ndk "C:\Users\wangm\AppData\Local\Android\Sdk\ndk\27.0.12077973"

if not defined NDK_DIR (
    echo [ERROR] Android NDK not found.
    echo         Set ANDROID_NDK_HOME, or install an NDK under SDK\ndk\.
    goto :fail
)
set "TOOLCHAIN=%NDK_DIR%\build\cmake\android.toolchain.cmake"
echo [2/4] NDK      : %NDK_DIR%

rem ---------- 3) configure ----------
if defined DO_CLEAN (
    if exist "%BUILD_DIR%" (
        echo [3/4] clean    : removing %BUILD_DIR%
        rmdir /s /q "%BUILD_DIR%"
    )
)

if not exist "%BUILD_DIR%\CMakeCache.txt" (
    echo [3/4] configure: generating build system...
    "%CMAKE_EXE%" -S "%ROOT%" -B "%BUILD_DIR%" -G Ninja ^
        -DCMAKE_TOOLCHAIN_FILE="%TOOLCHAIN%" ^
        -DANDROID_ABI=arm64-v8a ^
        -DANDROID_PLATFORM=android-24 ^
        -DANDROID_STL=c++_static ^
        -DCMAKE_BUILD_TYPE=Release
    if errorlevel 1 goto :fail
) else (
    echo [3/4] configure: reusing cache in %BUILD_DIR%
)

rem ---------- 4) build ----------
echo [4/4] build    : compiling...
"%CMAKE_EXE%" --build "%BUILD_DIR%" --parallel
if errorlevel 1 goto :fail

if not exist "%OUT_SO%" (
    echo [ERROR] build finished but artifact missing: %OUT_SO%
    goto :fail
)

echo.
echo ============================================================
echo  BUILD OK
echo ============================================================
for %%F in ("%OUT_SO%") do echo  artifact : %%~fF  (%%~zF bytes)

rem ---------- optional: push ----------
if defined DO_PUSH (
    set "ADB=%SDK%\platform-tools\adb.exe"
    if not exist "!ADB!" set "ADB=adb"
    echo.
    echo  push     : !ADB!
    "!ADB!" push "%OUT_SO%" /data/local/tmp/libciu.so
    if errorlevel 1 (
        echo [WARN] push failed - check device connection ^(adb devices^)
    ) else (
        echo  pushed   : /data/local/tmp/libciu.so
    )
)

echo.
endlocal
exit /b 0

rem ============================================================
rem  helpers
rem ============================================================
:try_cmake
if defined CMAKE_EXE exit /b 0
if exist %1 set "CMAKE_EXE=%~1"
exit /b 0

:try_ndk
if defined NDK_DIR exit /b 0
if exist %1\build\cmake\android.toolchain.cmake set "NDK_DIR=%~1"
exit /b 0

:fail
echo.
echo *** BUILD FAILED ***
endlocal
exit /b 1
