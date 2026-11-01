@echo off
setlocal
rem Resume from the last F5 input-replay checkpoint. The game replays every
rem saved input from boot at full host speed (--fast-replay; guest work and
rem timing are unchanged), then pauses at the checkpoint until you press a game
rem key. F5 during play saves a new checkpoint; the previous one is kept as
rem checkpoint.prev.txt.
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
rem Virtual memory card (slot 1); saves persist in Documents.
set "CARD_DIR=%USERPROFILE%\Documents\Haunting Ground Saves"
if not exist "%CARD_DIR%" mkdir "%CARD_DIR%"
set "CARD=%CARD_DIR%\memory-card-slot1.ps2"
set "TOC=%TEMP%\haunting-toc-probe\synthetic-index-pattern.bin"
set "CHECKPOINT=%TEMP%\haunting-toc-probe\checkpoint.txt"
set "RESUME=%TEMP%\haunting-toc-probe\checkpoint.resume.txt"
set "OUT=%TEMP%\haunting-toc-probe\manual-authentic.out.txt"
set "ERR=%TEMP%\haunting-toc-probe\manual-authentic.err.txt"
set "INPUT_LOG=%TEMP%\haunting-toc-probe\manual-authentic.input-events.txt"
if not exist "%TOC%" (
  echo Authentic TOC file is missing: %TOC%
  pause
  exit /b 1
)
if not exist "%CHECKPOINT%" (
  echo No checkpoint yet: press F5 during play to save one.
  pause
  exit /b 1
)
copy /y "%CHECKPOINT%" "%TEMP%\haunting-toc-probe\checkpoint.prev.txt" >nul
copy /y "%CHECKPOINT%" "%RESUME%" >nul
rem Keep the previous session's input recording and log for replaying stops.
set "SESSIONS=%TEMP%\haunting-toc-probe\sessions"
if not exist "%SESSIONS%" mkdir "%SESSIONS%"
for /f %%s in ('powershell -NoProfile -Command "Get-Date -Format yyyyMMdd-HHmmss"') do set "STAMP=%%s"
if exist "%INPUT_LOG%" move /y "%INPUT_LOG%" "%SESSIONS%\%STAMP%.input-events.txt" >nul
if exist "%ERR%" move /y "%ERR%" "%SESSIONS%\%STAMP%.err.txt" >nul
"%PLAY_EXE%" --toc-record "%TOC%" --memory-card "%CARD%" --memory-card-after-replay --slices 1000000000000 --gpu-resident --gpu-triangles --spu2-output-core 0 --input-events-file "%RESUME%" --input-replay-wait-live --fast-replay --checkpoint-file "%CHECKPOINT%" --input-log "%INPUT_LOG%" --checkpoint-every 1000000 --video-rate-log 1>"%OUT%" 2>"%ERR%"
endlocal
