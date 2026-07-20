@echo off
rem ============================================================
rem build.bat - rade_v2_voice.dll 一括ビルドスクリプト
rem
rem 使い方(これ1回でビルドできます):
rem   1. スタートメニューから
rem      「x64 Native Tools Command Prompt for VS 2022」を開く
rem      (Visual Studio 2022 の「C++によるデスクトップ開発」が必要)
rem   2. このリポジトリのフォルダに cd して build.bat を実行
rem
rem 実行内容:
rem   [1/3] FARGAN モデルデータのダウンロード(初回のみ、要ネット接続)
rem   [2/3] third_party\opus から opus.lib をビルド(初回のみ)
rem   [3/3] src_v2\rade_v2_voice.dll をビルド
rem
rem 2回目以降は済んだ手順を自動でスキップします。
rem opus.lib から作り直したい場合は third_party\opus\build を削除
rem してから再実行してください。
rem ============================================================
setlocal
cd /d "%~dp0"

set OPUS_MODEL_HASH=4ed9445b96698bad25d852e912b41495ddfa30c8dbc8a55f9cde5826ed793453

rem --- 前提チェック ---
where cl >nul 2>&1
if errorlevel 1 (
    echo エラー: cl.exe が見つかりません。
    echo   「x64 Native Tools Command Prompt for VS」から実行してください。
    echo   ^(スタートメニューで "x64 Native" を検索^)
    exit /b 1
)
where cmake >nul 2>&1
if errorlevel 1 (
    echo エラー: cmake が見つかりません。
    echo   Visual Studio Installer で「C++によるデスクトップ開発」に含まれる
    echo   「Windows 用 C++ CMake ツール」をインストールしてください。
    exit /b 1
)

rem --- リポジトリ一式の存在チェック ---
if not exist third_party\opus\dnn\download_model.bat (
    echo エラー: third_party\opus が見つかりません。
    echo   この build.bat はリポジトリ一式のルートに置いて実行するものです。
    echo   修正差分の zip を展開した場合は、既存のリポジトリのフォルダ
    echo   ^(例: C:\Users\yuichi\source\dev2\RadeCallTest\rade_v2_voice^)
    echo   に上書き展開してから、そのフォルダで build.bat を実行してください。
    echo   リポジトリがまだ無い場合は先に:
    echo     git clone https://github.com/yi-matsui/rade_v2_voice.git
    exit /b 1
)
if not exist src_v2\rade_v2_voice.c (
    echo エラー: src_v2\rade_v2_voice.c が見つかりません。
    echo   リポジトリ一式のルートで実行してください。
    exit /b 1
)

echo.
echo [1/3] FARGAN モデルデータの確認...
if exist third_party\opus\dnn\fargan_data.h (
    echo   取得済み。スキップします。
) else (
    echo   ダウンロードします（初回のみ、要ネット接続）...
    pushd third_party\opus\dnn
    call download_model.bat %OPUS_MODEL_HASH%
    popd
    if not exist third_party\opus\dnn\fargan_data.h (
        echo エラー: モデルデータの取得に失敗しました。
        echo   ネット接続を確認して再実行してください。
        exit /b 1
    )
)

echo.
echo [2/3] opus.lib の確認...
if exist third_party\opus\build\opus.lib (
    echo   ビルド済み。スキップします。
) else (
    echo   ビルドします（初回のみ、数分かかります）...
    pushd third_party\opus
    cmake -B build -G "NMake Makefiles" ^
        -DCMAKE_BUILD_TYPE=Release ^
        -DOPUS_BUILD_SHARED_LIBRARY=OFF ^
        -DOPUS_BUILD_TESTING=OFF ^
        -DOPUS_BUILD_PROGRAMS=OFF ^
        -DOPUS_DEEP_PLC=ON ^
        -DOPUS_DRED=ON ^
        -DOPUS_OSCE=ON
    if errorlevel 1 (popd & echo エラー: opus の CMake configure に失敗しました。 & exit /b 1)
    cmake --build build
    if errorlevel 1 (popd & echo エラー: opus のビルドに失敗しました。 & exit /b 1)
    popd
)

echo.
echo [3/3] rade_v2_voice.dll のビルド...
pushd src_v2
call build\build_rade_v2_voice.bat
if errorlevel 1 (popd & echo エラー: rade_v2_voice.dll のビルドに失敗しました。 & exit /b 1)
popd

echo.
echo ============================================================
echo ビルド成功: src_v2\rade_v2_voice.dll
echo   C# から使う場合は RadeV2Native.cs と同じ場所(実行フォルダ)に
echo   この DLL をコピーしてください。
echo ============================================================
endlocal
exit /b 0
