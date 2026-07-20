@echo off
rem ============================================================
rem build_lpcnet_demo.bat
rem   lpcnet_demo.c(音声<->特徴量変換ツール)をビルドする。
rem   radae リポジトリの dr-crowd ブランチ(src/lpcnet_demo.c)由来。
rem   依存は opus.lib のみ(RADE本体やPythonとは無関係の独立ツール)。
rem ============================================================
setlocal
if "%OPUS_SRC%"=="" set OPUS_SRC=..\third_party\opus
if "%OPUS_LIB%"=="" set OPUS_LIB=..\third_party\opus\build\opus.lib

if not exist lpcnet_demo.c (
    echo エラー: lpcnet_demo.c がありません。
    goto :done
)

set INCS=/I"%OPUS_SRC%\dnn" /I"%OPUS_SRC%\include" /I"%OPUS_SRC%\celt" /I"%OPUS_SRC%"

echo === コンパイル ===
cl /nologo /MD /utf-8 /Od %INCS% lpcnet_demo.c "%OPUS_LIB%" /Fe:lpcnet_demo.exe
if errorlevel 1 goto :done
echo === ビルド完了: lpcnet_demo.exe ===

:done
endlocal
