@echo off
rem build_test_api_v2.bat -- rade_api_v2 経由の統合テスト(EXE版、DLLではなく静的リンク)
setlocal
if "%OPUS_SRC%"=="" set OPUS_SRC=..\third_party\opus
if "%OPUS_LIB%"=="" set OPUS_LIB=..\third_party\opus\build\opus.lib

if not exist rade_v2_constants_data.c (
    echo エラー: rade_v2_constants_data.c がありません。先に実行:
    echo   python export_rade_v2_constants.py 250725\checkpoints\checkpoint_epoch_200.pth src_v2
    goto :done
)
if not exist sync_meta.txt (
    echo エラー: sync_*.f32 がありません。gen_sync_ref.py を先に実行してください。
    goto :done
)
if not exist tx_meta.txt (
    echo エラー: tx_*.f32 がありません。gen_tx_ref.py を先に実行してください。
    goto :done
)

set INCS=/I. /I"%OPUS_SRC%\dnn" /I"%OPUS_SRC%\include" /I"%OPUS_SRC%\celt" /I"%OPUS_SRC%"
set SRCS=test_api_v2.c rade_api_v2.c rade_rx_v2.c rade_extract_v2.c rade_eoo_v2.c rade_frame_sync.c rade_dec_v2.c rade_enc_v2.c rade_tx_v2.c rade_sync_v2.c rade_sync_data.c rade_dec_v2_data.c rade_enc_v2_data.c rade_v2_constants_data.c kiss_fft.c rade_bpf_v2.c 

echo === コンパイル開始 ===
cl /nologo /MD /utf-8 /Od %INCS% %SRCS% "%OPUS_LIB%" /Fe:test_api_v2.exe
if errorlevel 1 goto :done
echo === 実行開始 ===
test_api_v2.exe

:done
endlocal
