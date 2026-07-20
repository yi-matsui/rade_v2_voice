@echo off
rem build_decode_file.bat - build rade_v2_decode_file.exe (pure ASCII)
rem Links the .c sources directly (not rade_v2.dll) to avoid stale-DLL issues.

set OPUS=..\third_party\opus
set INC=/I. /I%OPUS%\dnn /I%OPUS%\include /I%OPUS%\celt /I%OPUS%

del *.obj rade_v2_decode_file.exe 2>nul

cl /nologo /MD /utf-8 /O2 %INC% ^
   rade_v2_decode_file.c ^
   rade_api_v2.c rade_rx_v2.c rade_extract_v2.c rade_eoo_v2.c ^
   rade_frame_sync.c rade_dec_v2.c rade_enc_v2.c rade_tx_v2.c ^
   rade_sync_v2.c rade_bpf_v2.c ^
   rade_sync_data.c rade_dec_v2_data.c rade_enc_v2_data.c ^
   rade_v2_constants_data.c kiss_fft.c ^
   %OPUS%\build\opus.lib ^
   /Fe:rade_v2_decode_file.exe

if errorlevel 1 goto :err
echo === build ok: rade_v2_decode_file.exe ===
goto :eof
:err
echo === build failed ===
