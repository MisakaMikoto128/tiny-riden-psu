@echo off
setlocal enabledelayedexpansion
cd /d "%~dp0"
if not exist build mkdir build

if "%APP%"=="" set "APP=tinyapp"

call "%~dp0env.bat"
if errorlevel 1 exit /b 1

set "CFLAGS=/nologo /c /O1 /Os /GS- /GR- /Gy /Gw /Zl /W3 /utf-8 /DUNICODE /D_UNICODE /Fo:build\"
set "LFLAGS=/nologo /SUBSYSTEM:WINDOWS /ENTRY:WinMainCRTStartup /OPT:REF /OPT:ICF /FILEALIGN:512 /NODEFAULTLIB"

set "SRCS="
for %%F in (src\*.cpp) do set "SRCS=!SRCS! %%F"
for %%F in (src\*.c) do set "SRCS=!SRCS! %%F"
for %%F in (transport\*.c) do set "SRCS=!SRCS! %%F"
if "!SRCS!"=="" (
  echo [x] no sources under src\
  exit /b 1
)

echo [1/2] compile!SRCS!
cl %CFLAGS% !SRCS!
if errorlevel 1 goto fail

echo [2/2] link
link %LFLAGS% /OUT:build\%APP%.exe build\*.obj kernel32.lib user32.lib gdi32.lib d2d1.lib dwrite.lib
if errorlevel 1 goto fail

echo.
for %%F in (build\%APP%.exe) do echo [ok] build\%APP%.exe  %%~zF bytes
exit /b 0

:fail
echo.
echo [x] build failed
exit /b 1
