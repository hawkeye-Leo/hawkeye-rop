@echo off
setlocal EnableExtensions

rem Copyright (C) 2026 Hawkeye - SPDX-License-Identifier: GPL-3.0-or-later
rem Copy linked HawkeyeRop.exe (and PDB when present) to repo bin/ for local runs.
set "TARGET=%~1"
set "PDB=%~2"
set "BINDIR=%~3"

if "%TARGET%"=="" (
  echo [Post-build] Missing target path argument.
  exit /b 1
)
if "%BINDIR%"=="" (
  echo [Post-build] Missing bin directory argument.
  exit /b 1
)

if not exist "%BINDIR%" mkdir "%BINDIR%"

copy /y "%TARGET%" "%BINDIR%HawkeyeRop.exe" >nul
if errorlevel 1 (
  echo [Post-build] Could not copy HawkeyeRop.exe to "%BINDIR%" - close Hawkeye ROP if it is running.
  exit /b 0
)

echo Copy HawkeyeRop.exe to Hawkeye ROP\bin

if exist "%PDB%" (
  copy /y "%PDB%" "%BINDIR%" >nul 2>&1
)

exit /b 0
