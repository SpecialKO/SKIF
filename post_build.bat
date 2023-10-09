@echo off

@REM Parameters given through the command line arguments:
@REM $1 == $(TargetPath)
@REM $2 == $(TargetFileName)

@REM Terminates any existing instance of the app
taskkill /im %2 /f /fi "STATUS eq RUNNING"

@REM Need to wait a sec for the process to end entirely
@REM The delay *between* each ping is 1 second, so 2 pings are needed
ping 127.0.0.1 -n 2 >nul

@REM Copies the executable to the desired target folder
copy /b /v /y %1 "D:\Games\Special K"

@REM Sign the executable (requires knowing TargetPath)
@REM signtool sign /n "Open Source Developer" /t http://time.certum.pl/ /fd sha256 /v %1