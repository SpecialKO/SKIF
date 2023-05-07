@echo off

msbuild SKIF.sln -t:Rebuild -p:Configuration=Release -p:Platform=x64 -m

if %ERRORLEVEL%==0 goto build_success
 
:build_fail
Pause
Exit /b 1 

:build_success
Exit /b 0