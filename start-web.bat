@echo off
rem Serves the Pedal Faster web preview at http://localhost:8080 and opens it.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\serve-web.ps1" %*
