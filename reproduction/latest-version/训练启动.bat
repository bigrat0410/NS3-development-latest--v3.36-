@echo off
chcp 65001 >nul
title Offline REINFORCE Training
echo ============================================================
echo   Launching WSL (Ubuntu) interactive training console...
echo ============================================================
echo.

wsl.exe -d Ubuntu -- bash -lc "exec /bin/bash /home/bigrat/workspace/ns-allinone-3.36.1/ns-3.36.1/scratch/reproduction/latest-version/run_offline_auto.sh"

echo.
echo (WSL session ended) Press any key to close this window...
pause >nul
