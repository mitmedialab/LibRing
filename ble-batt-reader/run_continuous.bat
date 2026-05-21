@echo off
echo Starting BLE Battery Reader Continuous Loop...
echo Press Ctrl+C to stop.

:loop
echo.
echo =========================================
echo Starting Node app at %time%
echo =========================================
node index.js
echo.
echo App exited. Restarting in 5 seconds...
timeout /t 5 /nobreak
goto loop