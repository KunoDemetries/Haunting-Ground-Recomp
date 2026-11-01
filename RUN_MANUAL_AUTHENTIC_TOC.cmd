@echo off
setlocal
cd /d "%~dp0"
rem Play a private copy so rebuilding build\Release\hg_game.exe never collides.
set "PLAY_DIR=%TEMP%\haunting-toc-probe\play"
set "PLAY_EXE=%PLAY_DIR%\hg_game.exe"
if not exist "%PLAY_DIR%" mkdir "%PLAY_DIR%"
copy /y "build\Release\hg_game.exe" "%PLAY_EXE%" >nul
fc /b "build\Release\hg_game.exe" "%PLAY_EXE%" >nul || (
  echo Could not refresh the play copy; close any running game window and retry.
  pause
  exit /b 1
)
set "HG_VU_WORKERS=3"
rem Virtual memory card (slot 1); saves persist in Documents.
set "CARD_DIR=%USERPROFILE%\Documents\Haunting Ground Saves"
if not exist "%CARD_DIR%" mkdir "%CARD_DIR%"
set "CARD=%CARD_DIR%\memory-card-slot1.ps2"
set "TOC=%TEMP%\haunting-toc-probe\synthetic-index-pattern.bin"
set "OUT=%TEMP%\haunting-toc-probe\manual-authentic.out.txt"
set "ERR=%TEMP%\haunting-toc-probe\manual-authentic.err.txt"
set "STARTED=%TEMP%\haunting-toc-probe\manual-authentic.started.txt"
set "DONE=%TEMP%\haunting-toc-probe\manual-authentic.completed.txt"
set "INPUT_LOG=%TEMP%\haunting-toc-probe\manual-authentic.input-events.txt"
if not exist "%TOC%" (
  echo Authentic TOC file is missing: %TOC%
  pause
  exit /b 1
)
if not exist "build\Release\hg_game.exe" (
  echo Rebuilt game executable is missing.
  pause
  exit /b 1
)
rem Keep the previous session's input recording and log for replaying stops.
set "SESSIONS=%TEMP%\haunting-toc-probe\sessions"
if not exist "%SESSIONS%" mkdir "%SESSIONS%"
for /f %%s in ('powershell -NoProfile -Command "Get-Date -Format yyyyMMdd-HHmmss"') do set "STAMP=%%s"
if exist "%INPUT_LOG%" move /y "%INPUT_LOG%" "%SESSIONS%\%STAMP%.input-events.txt" >nul
if exist "%ERR%" move /y "%ERR%" "%SESSIONS%\%STAMP%.err.txt" >nul
del /q "%OUT%" "%STARTED%" "%DONE%" 2>nul
>"%STARTED%" echo started %date% %time%
"%PLAY_EXE%" --toc-record "%TOC%" --memory-card "%CARD%" --slices 1000000000000 --gpu-resident --gpu-triangles --spu2-output-core 0 --input-log "%INPUT_LOG%" --checkpoint-file "%TEMP%\haunting-toc-probe\checkpoint.txt" --checkpoint-every 1000000 --video-rate-log 1>"%OUT%" 2>"%ERR%"
set "RC=%ERRORLEVEL%"
>"%DONE%" echo exit_code=%RC% finished %date% %time%
endlocal & exit /b %RC%
