@echo off
rem build_test_fargan.bat -- FARGAN 単体テスト
setlocal
if "%OPUS_SRC%"=="" set OPUS_SRC=..\third_party\opus
if "%OPUS_LIB%"=="" set OPUS_LIB=..\third_party\opus\build\opus.lib
set INCS=/I. /I"%OPUS_SRC%\dnn" /I"%OPUS_SRC%\include" /I"%OPUS_SRC%\celt" /I"%OPUS_SRC%"
cl /nologo /MD /utf-8 /Od %INCS% test_fargan.c "%OPUS_LIB%" /Fe:test_fargan.exe
endlocal
