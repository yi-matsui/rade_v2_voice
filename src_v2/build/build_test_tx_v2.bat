@echo off
rem build_test_tx_v2.bat -- RADE V2 送信部の数値検証(C=Python)
rem 前提: radae ルートで gen_tx_ref.py 実行済み、export_rade_v2_weights.py で
rem       rade_enc_v2_data.c/.h 生成済み。
setlocal
if "%OPUS_SRC%"=="" set OPUS_SRC=..\..\dr-radev2\opus
if "%OPUS_LIB%"=="" set OPUS_LIB=..\..\dr-radev2\opus\build\opus.lib

if not exist tx_meta.txt (
    echo エラー: tx_meta.txt がありません。先に python gen_tx_ref.py を実行してください。
    goto :done
)
if not exist rade_enc_v2_data.c (
    echo エラー: rade_enc_v2_data.c がありません。export_rade_v2_weights.py を確認してください。
    goto :done
)

set INCS=/I. /I"%OPUS_SRC%\dnn" /I"%OPUS_SRC%\include" /I"%OPUS_SRC%\celt" /I"%OPUS_SRC%"
echo コンパイル開始
cl /nologo /MD /utf-8 /Od %INCS% rade_tx_v2.c rade_enc_v2.c test_tx_v2.c rade_enc_v2_data.c "%OPUS_LIB%" /Fe:test_tx_v2.exe
if errorlevel 1 goto :done
echo 実行開始
test_tx_v2.exe

:done
endlocal
