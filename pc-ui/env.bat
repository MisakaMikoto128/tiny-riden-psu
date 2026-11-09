@echo off
rem env.bat - locate MSVC and Windows SDK, set INCLUDE/LIB/PATH
rem Do NOT call vcvars*.bat: it runs reg.exe, which restricted environments block.
rem Keep this file ASCII-only: CJK text in a .bat can merge lines under codepage 936.

set "VSROOT=%ProgramFiles%\Microsoft Visual Studio\2022\Community"
if not exist "%VSROOT%\VC\Tools\MSVC" set "VSROOT=%ProgramFiles%\Microsoft Visual Studio\2022\BuildTools"
if not exist "%VSROOT%\VC\Tools\MSVC" set "VSROOT=%ProgramFiles%\Microsoft Visual Studio\2022\Professional"
if not exist "%VSROOT%\VC\Tools\MSVC" (
  echo [x] MSVC not found. Install "Desktop development with C++" in VS2022.
  exit /b 1
)

set "VCDIR="
for /f "delims=" %%D in ('dir /b /ad /o-n "%VSROOT%\VC\Tools\MSVC" 2^>nul') do if not defined VCDIR set "VCDIR=%VSROOT%\VC\Tools\MSVC\%%D"

set "KITS=%ProgramFiles(x86)%\Windows Kits\10"
set "SDKVER="
for /f "delims=" %%D in ('dir /b /ad /o-n "%KITS%\Include" 2^>nul') do if not defined SDKVER set "SDKVER=%%D"
if not defined SDKVER (
  echo [x] Windows SDK not found.
  exit /b 1
)

set "PATH=%VCDIR%\bin\Hostx64\x86;%PATH%"
set "INCLUDE=%VCDIR%\include;%KITS%\Include\%SDKVER%\ucrt;%KITS%\Include\%SDKVER%\shared;%KITS%\Include\%SDKVER%\um;%KITS%\Include\%SDKVER%\winrt"
set "LIB=%VCDIR%\lib\x86;%KITS%\Lib\%SDKVER%\ucrt\x86;%KITS%\Lib\%SDKVER%\um\x86"
exit /b 0
