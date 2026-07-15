@echo off
rem ============================================================
rem build_test_eoo_v2.bat
rem   RADE V2 EOO 検出の数値検証(C=Python)
rem   kiss_fft を使うため、nopy の kiss_fft.c 等が必要。
rem
rem 前提:
rem   - x64 Native Tools Command Prompt
rem   - radae ルートで gen_eoo_ref.py を実行し
rem     eoo_pend.f32 / eoo_in.f32 / eoo_ref.f32 / eoo_meta.txt を生成済み
rem   - nopy の kiss_fft.c / kiss_fft.h / _kiss_fft_guts.h をこのフォルダに用意
rem     (FreeDVRADEReceiver_0614\native\deps\radae_nopy\src からコピー)
rem ============================================================
setlocal

rem kiss_fft 一式の在り処(必要ならコピー)
set KISS_SRC=..\..\FreeDVRadeRX\FreeDVRADEReceiver_0614\native\deps\radae_nopy\src

if not exist kiss_fft.c (
    if exist "%KISS_SRC%\kiss_fft.c" (
        echo kiss_fft 一式を %KISS_SRC% からコピーします...
        copy /y "%KISS_SRC%\kiss_fft.c" . >nul
        copy /y "%KISS_SRC%\kiss_fft.h" . >nul
        copy /y "%KISS_SRC%\_kiss_fft_guts.h" . >nul
    ) else (
        echo エラー: kiss_fft.c が見つかりません。
        echo   nopy の src から kiss_fft.c / kiss_fft.h / _kiss_fft_guts.h をこのフォルダに置いてください。
        goto :done
    )
)

if not exist eoo_meta.txt (
    echo エラー: eoo_meta.txt がありません。先に Python 基準を生成してください:
    echo   python gen_eoo_ref.py 250725\checkpoints\checkpoint_epoch_200.pth
    goto :done
)

echo === コンパイル&リンク ===
cl /nologo /MD /utf-8 /O2 /I. rade_eoo_v2.c test_eoo_v2.c kiss_fft.c /Fe:test_eoo_v2.exe || goto :done

echo === 実行 ===
test_eoo_v2.exe

:done
endlocal
