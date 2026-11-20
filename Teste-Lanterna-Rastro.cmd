@echo off
rem Teste da lanterna: abre o jogo com um ponto de parada no codigo da lanterna (0x697b83), que
rem anota no log (analysis/tlou/boot.log) onde ela esta na memoria. Com a lanterna acesa no jogo:
rem   python tools\flashlight_probe.py           mostra o contador ate a proxima falha
rem   python tools\flashlight_probe.py --force   faz a lanterna falhar agora
rem Agitar o controle (G no teclado, Share/Capture no controle) deve consertar a falha. Com a
rem opcao "Lanterna nunca falha" ligada no launcher a lanterna nunca falha sozinha.
cd /d "%~dp0"
set GOW3_EXEC_WATCH=697b83
set GOW3_EXEC_WATCH_SECONDS=3600
set GOW3_WATCH_HITS=50
start "" "launcher\TLOU Launcher.exe"
