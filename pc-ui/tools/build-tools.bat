@echo off
rem build-tools.bat - build dev-time verification tools
rem   shot.exe  : capture target window to shot.bmp
rem   click.exe : click by logical coords, or "0 0" to raise the window only
rem   keys.exe  : inject keystrokes, e.g. keys.exe "abc{enter}{ctrl+a}"
rem NOTE: obj files go to ..\build\tools\ so that build.bat's build\*.obj glob
rem never picks up these tools (they each define main).
setlocal
cd /d "%~dp0"
if not exist ..\build mkdir ..\build
if not exist ..\build\tools mkdir ..\build\tools

call "%~dp0..\env.bat"
if errorlevel 1 exit /b 1

cl /nologo /O1 /utf-8 shot.c  /Fe:shot.exe  /Fo:..\build\tools\ user32.lib gdi32.lib
if errorlevel 1 exit /b 1
cl /nologo /O1 /utf-8 click.c /Fe:click.exe /Fo:..\build\tools\ user32.lib gdi32.lib
if errorlevel 1 exit /b 1
cl /nologo /O1 /utf-8 keys.c  /Fe:keys.exe  /Fo:..\build\tools\ user32.lib shell32.lib
if errorlevel 1 exit /b 1
cl /nologo /O1 /utf-8 mb_test.c ..\transport\modbus_rtu.c /Fe:mb_test.exe /Fo:..\build\tools\ kernel32.lib
if errorlevel 1 exit /b 1
cl /nologo /O1 /utf-8 live_test.c ..\transport\modbus_rtu.c ..\transport\serial_io.c /Fe:live_test.exe /Fo:..\build\tools\ kernel32.lib
if errorlevel 1 exit /b 1
cl /nologo /O1 /utf-8 rate_test.c ..\transport\modbus_rtu.c ..\transport\serial_io.c /Fe:rate_test.exe /Fo:..\build\tools\ kernel32.lib
if errorlevel 1 exit /b 1

echo [ok] tools\shot.exe tools\click.exe tools\keys.exe tools\mb_test.exe tools\live_test.exe tools\rate_test.exe
