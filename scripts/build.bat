@echo off
REM Build NxfireDB (Windows, Visual Studio generator).
setlocal
set ROOT=%~dp0..
cmake -S "%ROOT%" -B "%ROOT%\build" -G "Visual Studio 17 2022" -A x64 || goto :err
cmake --build "%ROOT%\build" --config Release || goto :err
echo.
echo Build selesai. Hasil di %ROOT%\build\
endlocal
exit /b 0

:err
echo Build GAGAL.
exit /b 1
