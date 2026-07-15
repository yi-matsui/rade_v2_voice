@echo off
rem build_test_enc_v2.bat -- RADE V2 encoder単体の数値検証(C=Python)
setlocal
if "%OPUS_SRC%"=="" set OPUS_SRC=..\..\dr-radev2\opus
if "%OPUS_LIB%"=="" set OPUS_LIB=..\..\dr-radev2\opus\build\opus.lib

if not exist enc_meta.txt (
    echo エラー: enc_meta.txt がありません。先に python gen_enc_ref.py を実行してください。
    goto :done
)
set INCS=/I. /I"%OPUS_SRC%\dnn" /I"%OPUS_SRC%\include" /I"%OPUS_SRC%\celt" /I"%OPUS_SRC%"
echo コンパイル開始
cl /nologo /MD /utf-8 /Od %INCS% rade_enc_v2.c test_enc_v2.c rade_enc_v2_data.c "%OPUS_LIB%" /Fe:test_enc_v2.exe
if errorlevel 1 goto :done
echo 実行開始
test_enc_v2.exe

:done
endlocal
