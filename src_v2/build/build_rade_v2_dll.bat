@echo off
rem ============================================================
rem build_rade_v2_dll.bat
rem   RADE V2 全部品を束ねた rade_v2.dll をビルドする。
rem
rem 前提: 以下が全部 src_v2 に揃っていること
rem   rade_api_v2.c/.h, rade_rx_v2.c/.h, rade_extract_v2.c/.h,
rem   rade_eoo_v2.c/.h, rade_frame_sync.c/.h, rade_dec_v2.c/.h,
rem   rade_enc_v2.c/.h, rade_tx_v2.c/.h, rade_sync_v2.c/.h,
rem   rade_v2_comp.h, rade_v2_core.h,
rem   rade_sync_data.c/.h, rade_dec_v2_data.c/.h, rade_enc_v2_data.c/.h,
rem   rade_v2_constants_data.c/.h(export_rade_v2_constants.py 生成),
rem   kiss_fft.c/.h/_kiss_fft_guts.h(EOO用)
rem ============================================================
setlocal
if "%OPUS_SRC%"=="" set OPUS_SRC=..\third_party\opus
if "%OPUS_LIB%"=="" set OPUS_LIB=..\third_party\opus\build\opus.lib

if not exist rade_v2_constants_data.c (
    echo エラー: rade_v2_constants_data.c がありません。先に実行:
    echo   python export_rade_v2_constants.py 250725\checkpoints\checkpoint_epoch_200.pth src_v2
    goto :done
)
if not exist kiss_fft.c (
    echo エラー: kiss_fft.c がありません。nopy の src からコピーしてください。
    goto :done
)

set INCS=/I. /I"%OPUS_SRC%\dnn" /I"%OPUS_SRC%\include" /I"%OPUS_SRC%\celt" /I"%OPUS_SRC%"
set SRCS=rade_api_v2.c rade_rx_v2.c rade_extract_v2.c rade_eoo_v2.c rade_frame_sync.c rade_dec_v2.c rade_enc_v2.c rade_tx_v2.c rade_sync_v2.c rade_sync_data.c rade_dec_v2_data.c rade_enc_v2_data.c rade_v2_constants_data.c kiss_fft.c rade_bpf_v2.c 

echo === DLL ビルド開始 ===
cl /nologo /MD /utf-8 /O2 /Oi /fp:fast /arch:AVX /DNDEBUG /LD %INCS% %SRCS% "%OPUS_LIB%" /Fe:rade_v2.dll
if errorlevel 1 goto :done
echo === DLL ビルド完了: rade_v2.dll ===

:done
endlocal
