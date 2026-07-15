@echo off
setlocal
if "%..\..\dr-radev2\opus%"=="" set OPUS_SRC=..\..\dr-radev2\opus
if "%..\..\dr-radev2\opus\build\opus.lib%"=="" set OPUS_LIB=..\..\dr-radev2\opus\build\opus.lib
set INCS=/I"%..\..\dr-radev2\opus%\dnn" /I"%..\..\dr-radev2\opus%\include" /I"%..\..\dr-radev2\opus%\celt" /I"%..\..\dr-radev2\opus%"
cl /nologo /MD /utf-8 /Od %/I. /I"..\..\dr-radev2\opus\dnn" /I"..\..\dr-radev2\opus\include" /I"..\..\dr-radev2\opus\celt" /I"..\..\dr-radev2\opus"% test_fargan.c "%..\..\dr-radev2\opus\build\opus.lib%" /Fe:test_fargan.exe
