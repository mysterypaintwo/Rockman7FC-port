@echo off
rem ---------------------------------------------------------------------------
rem Rockman 7 FC - Windows installer
rem
rem   install-windows.cmd [game folder] [game folder]
rem
rem Drag the Japanese (rm7fc.exe) and/or English (mm7fc.exe) game folders onto
rem this file, or run it next to them. It installs into
rem %LOCALAPPDATA%\Rockman7FC, keeps the original folders untouched, and adds
rem "Rockman 7 FC" and "Rockman 7 FC Settings" shortcuts. Re-running it updates
rem an existing install and keeps your settings.
rem ---------------------------------------------------------------------------
setlocal EnableExtensions
set "HERE=%~dp0"
set "DEST=%LOCALAPPDATA%\Rockman7FC"
set "EXE=%DEST%\Rockman7FC.exe"

echo ===============================================
echo  Rockman 7 FC - Windows port installer
echo ===============================================
echo.

rem The binaries sit next to this script in a packaged release, or in build\
rem when run from the source tree.
set "SRC=%HERE%"
if not exist "%SRC%Rockman7FC.exe" set "SRC=%HERE%..\build\"
if not exist "%SRC%Rockman7FC.exe" (echo Rockman7FC.exe not found. Run tools/build.sh first. & goto :fail)
if not exist "%SRC%pxtone-proxy.dll" (echo pxtone-proxy.dll not found. Run tools/build.sh first. & goto :fail)

if not exist "%DEST%" mkdir "%DEST%"
copy /y "%SRC%Rockman7FC.exe" "%DEST%\" >nul || goto :fail
copy /y "%SRC%pxtone-proxy.dll" "%DEST%\" >nul || goto :fail
echo Installed to: %DEST%

set FOUND=0
rem folders given on the command line / dropped onto the script
:args
if "%~1"=="" goto :search
call :import "%~1"
shift
goto :args

:search
if %FOUND% neq 0 goto :shortcuts
for %%D in ("%HERE%..\final2" "%HERE%..\..\final2" "%HERE%..\mm7fc" "%HERE%..\..\mm7fc" ^
            "%USERPROFILE%\Downloads\final2" "%USERPROFILE%\Downloads\mm7fc") do (
  if exist "%%~D\rm7fc.exe" call :import "%%~D"
  if exist "%%~D\mm7fc.exe" call :import "%%~D"
)
if %FOUND%==0 (
  echo.
  echo No game folder found automatically. The Settings window will open:
  echo click "Add release..." and choose the folder with rm7fc.exe or mm7fc.exe.
)

:shortcuts
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "$s = New-Object -ComObject WScript.Shell;" ^
  "foreach ($dir in @([Environment]::GetFolderPath('Programs'), [Environment]::GetFolderPath('Desktop'))) {" ^
  "  $l = $s.CreateShortcut((Join-Path $dir 'Rockman 7 FC.lnk')); $l.TargetPath = '%EXE%'; $l.Arguments = '--play'; $l.WorkingDirectory = '%DEST%'; $l.IconLocation = '%EXE%,0'; $l.Save();" ^
  "  $l = $s.CreateShortcut((Join-Path $dir 'Rockman 7 FC Settings.lnk')); $l.TargetPath = '%EXE%'; $l.WorkingDirectory = '%DEST%'; $l.IconLocation = '%EXE%,0'; $l.Save() }" >nul 2>&1
if errorlevel 1 (echo Could not create shortcuts; start %EXE% directly.) else (echo Shortcuts: Start menu and Desktop)

echo.
echo Done. "Rockman 7 FC" plays the game; "Rockman 7 FC Settings" changes window
echo size, language, soundtrack, volume and keyboard/gamepad controls.
echo.
start "" "%EXE%"
pause
exit /b 0

:import
echo Adding release: %~1
"%EXE%" --import "%~1"
if errorlevel 1 (echo   ...failed, see %DEST%\port.log) else (set FOUND=1)
exit /b 0

:fail
echo.
echo Installation failed.
pause
exit /b 1
