@echo off
rem ============================================================
rem build_cs_smoketest.bat
rem   rade_v2.dll のC#疎通確認アプリを csc.exe でビルドする。
rem
rem 前提:
rem   - rade_v2.dll と rade_v2.lib が同じフォルダ、または実行時に
rem     rade_v2.dll が見つかる場所(exeと同じフォルダ推奨)にあること。
rem   - .NET Framework の csc.exe が使えること(x64 Native Tools
rem     Command Prompt であれば通常PATHが通っている)。
rem ============================================================
setlocal

copy /y ..\rade_v2.dll . >nul

where csc.exe >nul 2>nul
if errorlevel 1 (
    echo エラー: csc.exe が見つかりません。
    echo   .NET SDK か Visual Studio の Developer Command Prompt から実行してください。
    goto :done
)

echo === C# コンパイル ===
csc /nologo /platform:x64 /out:RadeV2SmokeTest.exe RadeComp.cs RadeV2Native.cs RadeV2Context.cs LoopbackTest.cs VoiceLoopbackTest.cs Program.cs
if errorlevel 1 goto :done

if not exist rade_v2.dll (
    echo 警告: rade_v2.dll がこのフォルダにありません。
    echo   src_v2\rade_v2.dll をこのフォルダにコピーしてから実行してください。
    goto :done
)

echo === 実行 ===
RadeV2SmokeTest.exe

:done
endlocal
