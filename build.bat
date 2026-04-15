@echo off
REM SessionGuard Premium Build Script (Multi-file)
REM Run from "Developer Command Prompt for VS"

echo Building Don't Reboot 11...

rc /v resources\dontreboot11.rc
cl /W4 /O2 /MT /DUNICODE /D_UNICODE src\main.cpp src\NotificationWindow.cpp src\RebootMonitor.cpp resources\dontreboot11.res /I include /link /SUBSYSTEM:WINDOWS /ENTRY:wWinMainCRTStartup USER32.lib SHELL32.lib GDI32.lib COMCTL32.lib OLE32.lib OLEAUT32.lib

if %ERRORLEVEL% EQU 0 (
    echo.
    echo Build Successful: main.exe
    move main.exe dontreboot11.exe
    mt.exe -manifest resources\SessionGuard.manifest -outputresource:dontreboot11.exe;#1
    del *.obj resources\dontreboot11.res
) else (
    echo.
    echo Build Failed.
)
pause
