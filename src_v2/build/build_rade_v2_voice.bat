@echo off
rem ============================================================
rem build_rade_v2_voice.bat
rem   rade_v2_voice.dll(rv_* インターフェース、V1+V2音声化)をビルド。
rem   src_v2 ディレクトリから実行すること:
rem     cd src_v2
rem     build\build_rade_v2_voice.bat
rem   (リポジトリルートの build.bat から呼ばれる場合は自動)
rem
rem   2026-07-13: V1(DR-NOPY, src_v1)対応版。V1本体5ファイルを追加。
rem   2026-07-20: 既定の opus パスを同梱の third_party\opus に変更。
rem   2026-07-20: /Od を /O2 /Oi /fp:fast /arch:AVX /DNDEBUG に変更。
rem     /Od のままだと RX 処理が入力 100ms あたり約 290ms かかり、
rem     リアルタイム受信で半分以上取りこぼす(間欠受信になる)。
rem     /O2 系で約 16ms/100ms まで改善(約18倍)。
rem     ※ /arch:AVX2 は非対応 CPU で例外 0xC000001D になるため AVX を採用。
rem        2011年以降の Intel/AMD CPU であればほぼ動作します。
rem ============================================================
setlocal
if "%OPUS_SRC%"=="" set OPUS_SRC=..\third_party\opus
if "%OPUS_LIB%"=="" set OPUS_LIB=..\third_party\opus\build\opus.lib
if "%V1_SRC%"==""   set V1_SRC=..\src_v1

if not exist rade_v2_voice.c (
    echo エラー: rade_v2_voice.c が見つかりません。
    echo   このバッチは src_v2 ディレクトリから実行してください:
    echo     cd src_v2
    echo     build\build_rade_v2_voice.bat
    exit /b 1
)
if not exist rade_v2_constants_data.c (
    echo エラー: rade_v2_constants_data.c がありません。
    exit /b 1
)
if not exist ht_coeff.h (
    echo エラー: ht_coeff.h がありません。
    exit /b 1
)
if not exist "%V1_SRC%\rade_api_nopy.c" (
    echo エラー: V1_SRC が不正です: %V1_SRC%
    exit /b 1
)
if not exist "%OPUS_LIB%" (
    echo エラー: opus.lib が見つかりません: %OPUS_LIB%
    echo   先に opus.lib をビルドしてください。リポジトリルートで:
    echo     build.bat
    echo   を実行すると opus.lib のビルドから本 DLL まで一括で行います。
    exit /b 1
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
cl /nologo /MD /utf-8 /O2 /Oi /fp:fast /arch:AVX /DNDEBUG /LD %INCS% %SRCS_V2% %SRCS_V1% "%OPUS_LIB%" /Fe:rade_v2_voice.dll
if errorlevel 1 exit /b 1
echo === ビルド完了: rade_v2_voice.dll ===
echo === エクスポート確認 ===
dumpbin /exports rade_v2_voice.dll | findstr rv_
endlocal
exit /b 0
