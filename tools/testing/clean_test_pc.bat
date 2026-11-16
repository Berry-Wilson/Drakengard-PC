@echo off
rem Double-click: lists the build tools and old releases on this PC, then asks before removing each.
rem For a list only, run: powershell -ExecutionPolicy Bypass -File clean_test_pc.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0clean_test_pc.ps1" -Clean
