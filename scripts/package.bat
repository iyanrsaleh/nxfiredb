@echo off
REM Paketkan hasil build menjadi folder + arsip rilis (tanpa source).
setlocal
set ROOT=%~dp0..
set VER=0.1.0
set OS=windows-x64
set OUT=%ROOT%\dist\nxfire-sdk-%VER%-%OS%
set STAGE=%ROOT%\dist\nxfire-sdk-%VER%

if not exist "%OUT%\bin" mkdir "%OUT%\bin"

copy /Y "%ROOT%\build\server\Release\nxfire-server.exe" "%OUT%\bin\" >nul || goto :err
copy /Y "%ROOT%\build\client\Release\nxfire-client.exe" "%OUT%\bin\" >nul || goto :err
copy /Y "%ROOT%\build\tools\nxfire\Release\nxfire.exe"   "%OUT%\bin\" >nul || goto :err
copy /Y "%ROOT%\build\tools\watch\Release\nxfire-watch.exe" "%OUT%\bin\" >nul || goto :err

if exist "%ROOT%\build\Release\nxfire.lib" copy /Y "%ROOT%\build\Release\nxfire.lib" "%OUT%\bin\" >nul

if exist "%ROOT%\README.md"         copy /Y "%ROOT%\README.md"         "%OUT%\README.md" >nul
if exist "%ROOT%\nxfire.json.example" copy /Y "%ROOT%\nxfire.json.example" "%OUT%\nxfire.json.example" >nul
copy /Y "%ROOT%\nxfire-sdk.json" "%OUT%\nxfire-sdk.json" >nul

REM Arsip .tar.gz (tar bawaan Windows 10+).
if exist "%STAGE%.tar.gz" del /Q "%STAGE%.tar.gz"
pushd "%ROOT%\dist"
tar -czf "nxfire-sdk-%VER%-%OS%.tar.gz" "nxfire-sdk-%VER%-%OS%" || goto :errpop
popd

echo.
echo Paket rilis siap di: %OUT%
echo Arsip: %ROOT%\dist\nxfire-sdk-%VER%-%OS%.tar.gz
endlocal
exit /b 0

:errpop
popd
:err
echo Gagal memaketkan. Build dulu: scripts\build.bat
exit /b 1
