@echo off
rem Place beside the private runtime executable, not in a Candidate installation.
if not exist "%~dp0Descent 3MASTERED.exe" exit /b 1
start "" /D "%~dp0." "%~dp0Descent 3MASTERED.exe" -setdir "%~dp0." -pilot Jeff -readonlysettings -windowed -nointro
