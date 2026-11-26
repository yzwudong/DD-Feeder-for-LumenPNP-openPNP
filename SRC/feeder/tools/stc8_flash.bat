@echo off
REM ============================================================
REM  stc8_flash.bat - flash STC8H1K17 via stc8isp
REM  stcflash.py is NOT compatible with STC8H erase protocol,
REM  use stc8isp.py (designed for STC8G/8H) instead.
REM  Usage: stc8_flash.bat <hex file>
REM  NOTE: stc8isp waits for a MCU power cycle (cold start)
REM        when it prints "Waiting for MCU power cycle ..."
REM  Change STC_PORT below if your COM port differs.
REM ============================================================
setlocal
set STC_PORT=COM33
set HEX=%~1
set BIN=%~dpn1.bin

if "%HEX%"=="" (
    echo usage: stc8_flash.bat ^<hex file^>
    exit /b 1
)

echo [1/2] hex to bin: %BIN%
python "%~dp0hex2bin.py" "%HEX%" "%BIN%"
if errorlevel 1 exit /b 1

echo [2/2] stc8isp flashing %BIN% on %STC_PORT%
python "%~dp0stc8isp.py" -p %STC_PORT% -f "%BIN%"
exit /b %errorlevel%