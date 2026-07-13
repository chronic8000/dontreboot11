@echo off
REM Don't Reboot 11 build script
REM Run from "x64 Native Tools Command Prompt for VS"

echo Building Don't Reboot 11...

rc /v resources\dontreboot11.rc
if %ERRORLEVEL% NEQ 0 goto :failed

cl /W4 /O2 /MT /EHsc /DUNICODE /D_UNICODE /std:c++20 ^
  src\main.cpp ^
  src\NotificationWindow.cpp ^
  src\RebootMonitor.cpp ^
  src\OrchestratorProtector.cpp ^
  src\UpdatePolicyManager.cpp ^
  src\Diagnostics.cpp ^
  resources\dontreboot11.res ^
  /I include ^
  /link /SUBSYSTEM:WINDOWS /ENTRY:wWinMainCRTStartup ^
  USER32.lib SHELL32.lib GDI32.lib COMCTL32.lib OLE32.lib OLEAUT32.lib ^
  ADVAPI32.lib TDH.lib RUNTIMEOBJECT.lib

if %ERRORLEVEL% EQU 0 (
    echo.
    echo Build Successful: dontreboot11.exe
    move /Y main.exe dontreboot11.exe >nul 2>&1
    del *.obj resources\dontreboot11.res 2>nul
    goto :done
)

:failed
echo.
echo Build Failed.
exit /b 1

:done
