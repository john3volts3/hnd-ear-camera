@echo off
rem Starts the HND Ear Camera quick test viewer (Python 3.8+ required, no extra package).
rem Connect the PC Wi-Fi to the camera network (HNDEC_...) first.
rem The viewer stops by itself when the browser page is closed.
where python >nul 2>nul
if %errorlevel%==0 (
    python "%~dp0hnd_viewer.py" %*
) else (
    py -3 "%~dp0hnd_viewer.py" %*
)
rem keep the window open only when something went wrong
if errorlevel 1 pause
