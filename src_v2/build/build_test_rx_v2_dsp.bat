@echo off
rem ============================================================
rem build_test_rx_v2_dsp.bat
rem   RADE V2 受信機 DSP 部の数値検証(C=Python)
rem   ※ この段は nnet(opus)不要。rade_rx_v2.c と test だけでビルドできる。
rem
rem 前提:
rem   - x64 Native Tools Command Prompt
rem   - radae ルートで gen_rxdsp_ref.py を実行し
rem     rxdsp_in.f32 / rxdsp_ref.f32 / rxdsp_meta.txt を生成済み
rem     (このバッチと同じフォルダに置くこと)
rem ============================================================
setlocal

if not exist rxdsp_meta.txt (
    echo エラー: rxdsp_meta.txt がありません。先に Python 基準を生成してください:
    echo   python gen_rxdsp_ref.py 250725\checkpoints\checkpoint_epoch_200.pth
    goto :done
)

echo === コンパイル(nnet不要)===
cl /nologo /MD /utf-8 /O2 /I. rade_rx_v2.c test_rx_v2_dsp.c || goto :done

echo === 実行 ===
test_rx_v2_dsp.exe

:done
endlocal
