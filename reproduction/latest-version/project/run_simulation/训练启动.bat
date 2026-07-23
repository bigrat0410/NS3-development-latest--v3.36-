@echo off
chcp 65001 >nul
title Offline REINFORCE Training
echo ============================================================
echo   Launching WSL (Ubuntu) interactive training console...
echo   A "UNC path not supported" note here is harmless.
echo ============================================================
echo.

wsl.exe -d Ubuntu -- bash -lc "cd /home/bigrat/workspace/ns-allinone-3.36.1/ns-3.36.1/scratch/reproduction/latest-version/project/run_simulation && chmod +x run_offline_auto.sh && ./run_offline_auto.sh"

echo.
echo (WSL session ended) Press any key to close this window...
pause >nul
