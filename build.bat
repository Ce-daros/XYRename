@echo off
rem ============================================================
rem  XYRename.exe build script
rem  Requires MSYS2 MinGW-w64 (gcc / windres)
rem ============================================================
setlocal
cd /d "%~dp0"

set PATH=C:\msys64\mingw64\bin;%PATH%

if not exist build mkdir build

echo [1/2] compiling resources ...
windres src\app.rc -o build\app.o --include-dir=res --codepage=65001 -F pe-x86-64
if errorlevel 1 goto fail

echo [2/2] compiling program ...
gcc -O2 -municode -mwindows -Wall -s -o XYRename.exe src\main.c src\help.c src\about.c src\topics.c src\exif.c src\regex.c build\app.o -lcomctl32 -lcomdlg32 -lshell32 -lshlwapi -lole32 -luuid -lgdi32 -luser32
if errorlevel 1 goto fail

echo.
echo Build OK: XYRename.exe
dir /b XYRename.exe
goto end

:fail
echo.
echo *** BUILD FAILED ***
exit /b 1

:end
endlocal
