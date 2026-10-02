@echo off
rem Desktop unit test of the NMEA parser.
setlocal
if "%VS8%"=="" set VS8=C:\Program Files (x86)\Microsoft Visual Studio 8
set VC=%VS8%\VC
set PATH=%VS8%\Common7\IDE;%VC%\bin;%PATH%
cd /d "%~dp0"
if not exist obj\test mkdir obj\test
"%VC%\bin\cl.exe" /nologo /W3 /MT /TP test_gps.cpp util.cpp /Foobj\test\ /Feobj\test\test_gps.exe ^
  /DUNICODE /D_UNICODE /DWIN32 /D_CRT_SECURE_NO_WARNINGS /I"%VC%\include" /I"%VC%\PlatformSDK\Include" ^
  /link /LIBPATH:"%VC%\lib" /LIBPATH:"%VC%\PlatformSDK\Lib" user32.lib kernel32.lib
if errorlevel 1 exit /b 1
obj\test\test_gps.exe
