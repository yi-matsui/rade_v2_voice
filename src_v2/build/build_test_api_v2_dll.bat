@echo off
rem build_test_api_v2_dll.bat -- rade_v2.dll を動的リンクしてtest_api_v2を実行
rem   rade_api_v2.c(実装)は含めず、rade_v2.lib(インポートライブラリ)経由でリンク。
rem   これで実行時にrade_v2.dllが正しくロード・呼び出しできるか確認する。
setlocal
if not exist rade_v2.dll (
    echo エラー: rade_v2.dll がありません。先に build_rade_v2_dll.bat を実行してください。
    goto :done
)
if not exist rade_v2.lib (
    echo エラー: rade_v2.lib がありません。
    goto :done
)

echo === コンパイル(DLL動的リンク版) ===
cl /nologo /MD /utf-8 /Od /I. test_api_v2.c rade_v2.lib /Fe:test_api_v2_dll.exe
if errorlevel 1 goto :done

echo === 実行 ===
test_api_v2_dll.exe

:done
endlocal
