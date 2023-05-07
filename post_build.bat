@echo off

@REM $(TargetPath) should be in %1 (the first parameter)

@REM Copies the executable to the desired target folder
copy /b /v /y %1 "D:\Games\Special K"

@REM Sign the executable (requires knowing TargetPath)
@REM signtool sign /n "Open Source Developer" /t http://time.certum.pl/ /fd sha256 /v "$(TargetPath)"