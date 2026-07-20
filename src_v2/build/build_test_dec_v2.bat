@echo off
rem ============================================================
rem build_test_dec_v2.bat
rem   RADE V2 stateful decoder の数値検証(C=Python)
rem   opus.lib(nnet実装)と V2 decoder 重み rade_dec_v2_data.c が必要。
rem
rem 前提:
rem   - x64 Native Tools Command Prompt
rem   - src_v2 に rade_dec_v2_data.c/.h(export生成)がある
rem   - rade_v2_core.h がある(RADEDecV2 の前方 typedef)
rem   - radae ルートで gen_dec_ref.py を実行し dec_in/ref .f32, dec_meta.txt 生成済み
rem ============================================================
setlocal
if "%OPUS_SRC%"=="" set OPUS_SRC=..\third_party\opus
if "%OPUS_LIB%"=="" set OPUS_LIB=..\third_party\opus\build\opus.lib

if not exist dec_meta.txt (
    echo エラー: dec_meta.txt がありません。先に Python 基準を生成:
    echo   python gen_dec_ref.py 250725\checkpoints\checkpoint_epoch_200.pth
    goto :done
)
if not exist rade_dec_v2_data.c (
    echo エラー: rade_dec_v2_data.c がありません（export生成物）。
    goto :done
)

set INCS=/I. /I"%OPUS_SRC%\dnn" /I"%OPUS_SRC%\include" /I"%OPUS_SRC%\celt" /I"%OPUS_SRC%"
echo === コンパイル,リンク ===
cl /nologo /MD /utf-8 /O2 %INCS% rade_dec_v2.c test_dec_v2.c rade_dec_v2_data.c "%OPUS_LIB%" /Fe:test_dec_v2.exe || goto :done
echo === 実行 ===
test_dec_v2.exe
:done
endlocal
