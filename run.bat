@echo off
REM ======================================================================
REM  Student Record Management System Using B-Tree
REM  run.bat - compile (if needed) and start the C server on Windows
REM ======================================================================

echo.
echo =========================================================
echo   Student Record Management System Using B-Tree
echo   Backend: C   Index: B-Tree (T = 3, max 5 keys per node)
echo =========================================================
echo.

where gcc >nul 2>nul
if %errorlevel%==0 (
    goto :compile
)
echo [INFO] gcc was not found - running Python backend server instead...
python server.py --port 8080 --data backend\students.dat --frontend frontend
if %errorlevel% neq 0 (
    echo [ERROR] Failed to start Python server.
    pause
    exit /b 1
)
exit /b 0

:compile
if not exist student_server.exe (
    echo [1/2] Compiling the C backend ...
    gcc -Wall -Wextra -O2 backend\*.c -o student_server.exe -lws2_32
    if %errorlevel% neq 0 (
        echo.
        echo [ERROR] Compilation failed - see the messages above.
        pause
        exit /b 1
    )
) else (
    echo [1/2] student_server.exe already exists - skipping compilation.
)

echo [2/2] Starting the HTTP server ...
echo.
echo     Website : http://localhost:8080
echo     API     : http://localhost:8080/api/students
echo.
echo     Press Ctrl+C to stop the server. Records are saved to
echo     backend\students.dat automatically.
echo.

student_server.exe --port 8080 --data backend\students.dat --frontend frontend

pause

