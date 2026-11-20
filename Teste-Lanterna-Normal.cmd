@echo off
rem Teste da lanterna numa partida normal (tools/flashlight_log.py): abre o launcher e uma janela
rem que grava, segundo a segundo, o contador da lanterna ate a proxima falha, e anota quando ele
rem recomeca do inicio, quando o jogo forca uma falha e quando a lanterna falha sozinha.
rem Jogue normalmente com "Lanterna nunca falha" DESLIGADA, de preferencia 15 minutos ou mais com
rem a lanterna acesa, e feche o jogo. A gravacao fica em analysis\tlou\flashlight-log.csv.
cd /d "%~dp0"
set GOW3_EXEC_WATCH_FILE=%~dp0analysis\tlou\flashlight-watch.txt
set GOW3_WATCH_HITS=2000
if not exist "analysis\tlou" mkdir "analysis\tlou"
echo 697b83> "analysis\tlou\flashlight-watch.txt"
set PY=python
if exist "python\python.exe" set PY=python\python.exe
start "Lanterna (gravando)" %PY% -I "tools\flashlight_log.py"
if exist "TLOU Launcher.exe" (start "" "TLOU Launcher.exe") else (start "" "launcher\TLOU Launcher.exe")
