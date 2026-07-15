@echo off
rem ============================================================
rem build_rade_v2_voice.bat
rem   rade_v2_voice.dll(rv_* インターフェース、V1+V2音声化)をビルド。
rem   2026-07-13: V1(DR-NOPY, src_v1)対応版。V1本体5ファイルを追加。
rem   V1のパスはルート直下 src_v1(src_v2の兄弟フォルダ)を想定。
rem ============================================================
setlocal
if "%OPUS_SRC%"=="" set OPUS_SRC=..\..\dr-radev2\opus
if "%OPUS_LIB%"=="" set OPUS_LIB=..\..\dr-radev2\opus\build\opus.lib
if "%V1_SRC%"==""   set V1_SRC=..\src_v1

if not exist rade_v2_constants_data.c (
    echo エラー: rade_v2_constants_data.c がありません。
    goto :done
)
if not exist ht_coeff.h (
    echo エラー: ht_coeff.h がありません(DR-NOPY の src からコピー)。
    goto :done
)
if not exist "%V1_SRC%\rade_api_nopy.c" (
    echo エラー: V1_SRC が不正です: %V1_SRC%
    goto :done
)

set INCS=/I. /I"%V1_SRC%" /I"%OPUS_SRC%\dnn" /I"%OPUS_SRC%\include" /I"%OPUS_SRC%\celt" /I"%OPUS_SRC%"

rem V2 本体
set SRCS_V2=rade_v2_voice.c rade_api_v2.c rade_rx_v2.c rade_extract_v2.c rade_eoo_v2.c rade_frame_sync.c rade_dec_v2.c rade_enc_v2.c rade_tx_v2.c rade_sync_v2.c rade_sync_data.c rade_dec_v2_data.c rade_enc_v2_data.c rade_v2_constants_data.c kiss_fft.c rade_bpf_v2.c

rem V1 本体(DR-NOPY、src_v1)。radae_rx.c/radae_tx.c/ch.c等の単体テスト
rem ツール(main()あり)は含めない。
rem 2026-07-13 追記: 正しいV1本体は rade_api_nopy.c 系(旧rade_api.cは
rem Python埋め込み版だったので廃止)。kiss_fft.c はV2側と完全同一
rem (fc /b で確認済み)なのでV1側からは除外し重複リンク警告を解消。
set SRCS_V1="%V1_SRC%\rade_api_nopy.c" "%V1_SRC%\rade_tx.c" "%V1_SRC%\rade_rx.c" "%V1_SRC%\rade_dsp.c" "%V1_SRC%\rade_ofdm.c" "%V1_SRC%\rade_bpf.c" "%V1_SRC%\rade_acq.c" "%V1_SRC%\rade_dec.c" "%V1_SRC%\rade_dec_data.c" "%V1_SRC%\rade_enc.c" "%V1_SRC%\rade_enc_data.c"

echo === rade_v2_voice.dll ビルド開始(V1+V2) ===
cl /nologo /MD /utf-8 /Od /LD %INCS% %SRCS_V2% %SRCS_V1% "%OPUS_LIB%" /Fe:rade_v2_voice.dll
if errorlevel 1 goto :done
echo === ビルド完了: rade_v2_voice.dll ===
echo === エクスポート確認 ===
dumpbin /exports rade_v2_voice.dll | findstr rv_
:done
endlocal
