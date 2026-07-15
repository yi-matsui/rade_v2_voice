@echo off
rem ============================================================
rem build_opus.bat - RADE用 libopus (FARGAN/DRED/OSCE有効) MSVCビルド
rem
rem 前提:
rem   - 「x64 Native Tools Command Prompt for VS」から実行すること
rem   - git / cmake / tar / powershell がPATHにあること (tar/powershellは標準)
rem
rem BuildOpus.cmake でピン留めされたコミットを使用する:
rem   940d4e5af64351ca8ba8390df3f555484c567fbb
rem
rem 【重要】opus は FARGAN/DRED/OSCE のモデルデータ(dnn/fargan_data.h 等)を
rem   リポジトリに含まず、ビルド前に download_model で取得する必要がある。
rem   本家の autogen.sh が「dnn/download_model.sh <hash>」を実行しているのと
rem   同じことを Windows 用の download_model.bat で行う。
rem   このコミット(940d4e5a)が要求するモデルハッシュ:
rem     4ed9445b96698bad25d852e912b41495ddfa30c8dbc8a55f9cde5826ed793453
rem
rem 出力: opus\build\opus.lib (静的ライブラリ, /MD)
rem ============================================================
setlocal

set OPUS_COMMIT=940d4e5af64351ca8ba8390df3f555484c567fbb
set OPUS_MODEL_HASH=4ed9445b96698bad25d852e912b41495ddfa30c8dbc8a55f9cde5826ed793453

if not exist opus (
    echo [1/4] opus を clone しています...
    git clone https://github.com/xiph/opus.git || goto :error
)

cd opus
echo [1/4] ピン留めコミットへ checkout: %OPUS_COMMIT%
git fetch --all >nul 2>&1
git checkout %OPUS_COMMIT% || goto :error

echo.
echo [2/4] モデルデータ取得 (dnn\fargan_data.h 等)...
rem download_model.bat は dnn 配下で実行し、dnn\ 直下に展開させる
pushd dnn
if not exist fargan_data.h (
    call download_model.bat %OPUS_MODEL_HASH% || (popd & goto :error)
) else (
    echo   fargan_data.h は取得済み。スキップします。
)
popd

rem 注意: 本家の opus-nnet.h.diff は GCC の visibility 前提のパッチであり、
rem       opus を rade.dll に静的リンクする MSVC 構成では不要のため適用しない。

echo.
echo [3/4] CMake configure (NMake, static, /MD)...
cmake -B build -G "NMake Makefiles" ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DOPUS_BUILD_SHARED_LIBRARY=OFF ^
    -DOPUS_BUILD_TESTING=OFF ^
    -DOPUS_BUILD_PROGRAMS=OFF ^
    -DOPUS_DEEP_PLC=ON ^
    -DOPUS_DRED=ON ^
    -DOPUS_OSCE=ON ^
    || goto :error

echo.
echo [4/4] ビルド中...
cmake --build build || goto :error

echo.
echo ==== 成功: %CD%\build\opus.lib ====
echo FARGAN シンボル確認 (fargan が出ればOK):
dumpbin /symbols build\opus.lib | findstr /i "fargan_init"
endlocal
exit /b 0

:error
echo.
echo ==== エラー: opus のビルドに失敗しました ====
endlocal
exit /b 1
