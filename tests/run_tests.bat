@echo off
setlocal enabledelayedexpansion
REM Test runner for mronc (Windows)
REM Run from the project root directory

set MRONC=mronc.exe
if not exist %MRONC% (
    echo ERROR: mronc.exe not found. Run 'make' first.
    exit /b 1
)

set PASS=0
set FAIL=0
set TOTAL=0

echo === MRON -^> JSON tests ===
call :run_test "Basic key-value pairs"    tests\test_basic.mron         tests\test_basic.expected.json
call :run_test "Comments"                 tests\test_comments.mron      tests\test_comments.expected.json
call :run_test "All value types"          tests\test_types.mron         tests\test_types.expected.json
call :run_test "CSV-style lists"          tests\test_csv.mron           tests\test_csv.expected.json
call :run_test "Nested structures"        tests\test_nested.mron        tests\test_nested.expected.json
call :run_test "Empty structures"         tests\test_empty.mron         tests\test_empty.expected.json
call :run_test "Full README example"      tests\test_full_example.mron  tests\test_full_example.expected.json

echo.
echo === JSON -^> MRON tests ===
call :run_test "JSON to MRON conversion"  tests\test_json_to_mron.json  tests\test_json_to_mron.expected.mron

echo.
echo === Error handling tests ===
call :run_error_test "Unterminated string"    tests\test_err_unterminated_string.mron
call :run_error_test "Invalid key - number"   tests\test_err_invalid_key.mron
call :run_error_test "Bad value - identifier" tests\test_err_bad_value.mron
call :run_error_test "CSV value mismatch"     tests\test_err_csv_mismatch.mron
call :run_error_test "Unclosed brace"         tests\test_err_unclosed_brace.mron

echo.
echo === Results: %PASS%/%TOTAL% passed, %FAIL% failed ===

if %FAIL% gtr 0 exit /b 1
exit /b 0

:run_test
set /a TOTAL+=1
set "desc=%~1"
set "input=%~2"
set "expected=%~3"

%MRONC% "%input%" > tests\_actual.tmp 2>nul
if errorlevel 1 (
    echo   FAIL: %desc% - mronc returned error
    set /a FAIL+=1
    goto :eof
)

fc /b tests\_actual.tmp "%expected%" >nul 2>&1
if errorlevel 1 (
    echo   FAIL: %desc%
    set /a FAIL+=1
) else (
    echo   PASS: %desc%
    set /a PASS+=1
)
del tests\_actual.tmp 2>nul
goto :eof

:run_error_test
set /a TOTAL+=1
set "desc=%~1"
set "input=%~2"

%MRONC% "%input%" >nul 2>&1
if errorlevel 1 (
    echo   PASS: %desc% - correctly reported error
    set /a PASS+=1
) else (
    echo   FAIL: %desc% - expected error but succeeded
    set /a FAIL+=1
)
goto :eof
