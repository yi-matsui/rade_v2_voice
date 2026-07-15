@echo off
rem build_test_bpf_v2.bat - build and run BPF verification (pure ASCII)
del *.obj test_bpf_v2.exe 2>nul
cl /nologo /MD /utf-8 /Od /I. rade_bpf_v2.c test_bpf_v2.c /Fe:test_bpf_v2.exe
if errorlevel 1 goto :err
test_bpf_v2.exe
goto :eof
:err
echo build failed
