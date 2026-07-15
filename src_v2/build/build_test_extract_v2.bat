@echo off
rem ============================================================
rem build_test_extract_v2.bat
rem   RADE V2 _extract_symbol の数値検証(C=Python)
rem   ※ FFT 不要(Wfwd 行列積のみ)。rade_extract_v2.c と test だけ。
rem
rem 前提:
rem   - x64 Native Tools Command Prompt
rem   - radae ルートで gen_extract_ref.py を実行し
rem     extract_wfwd/rxbuf/params/zhat/symtd.f32, extract_meta.txt を生成済み
rem     (このフォルダに置くこと)
rem ============================================================
setlocal
if not exist extract_meta.txt (
    echo エラー: extract_meta.txt がありません。先に Python 基準を生成:
    echo   python gen_extract_ref.py 250725\checkpoints\checkpoint_epoch_200.pth
    goto :done
)
echo === コンパイル,リンク ===
cl /nologo /MD /utf-8 /O2 /I. rade_extract_v2.c test_extract_v2.c /Fe:test_extract_v2.exe || goto :done
echo === 実行 ===
test_extract_v2.exe
:done
endlocal
