@echo off
setlocal enabledelayedexpansion
REM Stress test for mronc (Windows)
REM Generates a large .mron file, converts to JSON, converts back, and checks timing.
REM Run from the project root directory.

set MRONC=mronc.exe
if not exist %MRONC% (
    echo ERROR: mronc.exe not found. Run 'make' first.
    exit /b 1
)

set GENSCRIPT=tests\_gen_stress.py
set MRON_FILE=tests\_stress.mron
set JSON_FILE=tests\_stress.json
set ROUNDTRIP_FILE=tests\_stress_rt.mron

echo === Stress Test ===
echo.

REM --- Generate ---
echo [1/5] Generating large MRON file...
python "%GENSCRIPT%" "%MRON_FILE%"
if errorlevel 1 (
    echo FAIL: generator script failed
    exit /b 1
)
for %%A in (%MRON_FILE%) do set FILESIZE=%%~zA
echo       Generated %MRON_FILE% (!FILESIZE! bytes)

REM --- MRON to JSON ---
echo [2/5] MRON -^> JSON ...
set T0=%TIME%
%MRONC% "%MRON_FILE%" -o "%JSON_FILE%"
if errorlevel 1 (
    echo FAIL: mronc returned error on MRON-^>JSON
    goto :cleanup
)
set T1=%TIME%
call :elapsed "%T0%" "%T1%" ms_mron_to_json
echo       Completed in !ms_mron_to_json! ms
for %%A in (%JSON_FILE%) do set JSONSIZE=%%~zA
echo       Produced %JSON_FILE% (!JSONSIZE! bytes)

REM --- JSON to MRON ---
echo [3/5] JSON -^> MRON ...
set T0=%TIME%
%MRONC% "%JSON_FILE%" -o "%ROUNDTRIP_FILE%"
if errorlevel 1 (
    echo FAIL: mronc returned error on JSON-^>MRON
    goto :cleanup
)
set T1=%TIME%
call :elapsed "%T0%" "%T1%" ms_json_to_mron
echo       Completed in !ms_json_to_mron! ms

REM --- Round-trip check: MRON2 -> JSON2, compare with JSON1 ---
echo [4/5] Round-trip verification (MRON-^>JSON-^>MRON-^>JSON) ...
%MRONC% "%ROUNDTRIP_FILE%" -o tests\_stress_rt.json
if errorlevel 1 (
    echo FAIL: mronc returned error on second MRON-^>JSON pass
    goto :cleanup
)
fc /b "%JSON_FILE%" tests\_stress_rt.json >nul 2>&1
if errorlevel 1 (
    echo FAIL: round-trip JSON output differs
    goto :cleanup
) else (
    echo       Round-trip OK - JSON outputs match
)

REM --- Summary ---
echo [5/5] Summary
echo       MRON-^>JSON : !ms_mron_to_json! ms  (%MRON_FILE% !FILESIZE! bytes)
echo       JSON-^>MRON : !ms_json_to_mron! ms  (%JSON_FILE% !JSONSIZE! bytes)
echo.
echo PASS: stress test completed successfully
goto :cleanup

:cleanup
del "%MRON_FILE%" 2>nul
del "%JSON_FILE%" 2>nul
del "%ROUNDTRIP_FILE%" 2>nul
del tests\_stress_rt.json 2>nul
goto :eof

REM --- Helper: compute elapsed time in milliseconds ---
:elapsed
set "start=%~1"
set "stop=%~2"
for /F "tokens=1-4 delims=:." %%a in ("%start%") do (
    set /a "s_ms=(((%%a*60)+1%%b %% 100)*60+1%%c %% 100)*100 + 1%%d %% 100"
)
for /F "tokens=1-4 delims=:." %%a in ("%stop%") do (
    set /a "e_ms=(((%%a*60)+1%%b %% 100)*60+1%%c %% 100)*100 + 1%%d %% 100"
)
set /a "diff=e_ms - s_ms"
if !diff! lss 0 set /a "diff+=8640000"
set /a "diff*=10"
set "%~3=!diff!"
goto :eof
