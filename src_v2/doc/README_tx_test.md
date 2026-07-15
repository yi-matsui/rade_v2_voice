# 送信側(rade_enc_v2 / rade_tx_v2) 検証手順

## できたもの
- rade_enc_v2.c/.h ── V2 stateful encoder(V1 rade_enc.c を移植)
  - decoderで判明した知見を最初から適用: 全層n()にclamp[-1,1]、
    GRU state(gruN_state)は無clampのまま保持し別バッファでclamp
  - conv2〜5 は dilation=2(V1のcompute_generic_conv1d_dilationを流用)
  - bottleneck引数でACTIVATION_TANH/LINEARを切替(Python if文と対応)
- rade_tx_v2.c/.h ── RADEv2Transmitter.transmit_frame の移植
  - encoder -> QPSK map(z[2k]=real,z[2k+1]=imag) -> IDFT(Winv行列積)
    -> CP挿入。V2はpilots無しでV1より大幅に単純。
  - **Linux側でnumpyと照合し変調ロジック(QPSK map/IDFT/CP)は確定済み**
    (最大相対誤差1.8e-05, encoderを介さない直接テスト)

## 検証の2段構え
1. gen_enc_ref.py + test_enc_v2.c: encoder単体(NN部分)
2. gen_tx_ref.py + test_tx_v2.c: 送信全体(encoder+変調+CP)

## 手順(Windows)

### 前提: rade_enc_v2_data.c/.h の生成
export_rade_v2_weights.py は既にenc/dec/syncをまとめて出力するので、
既存の実行で rade_enc_v2_data.c/.h も既に src_v2 にあるはず(未確認なら
再実行: python export_rade_v2_weights.py 250725\checkpoints\checkpoint_epoch_200.pth 250725a_ml_sync src_v2)

### 1. encoder単体
```
（radaeルート）copy /y src_v2\gen_enc_ref.py .
python gen_enc_ref.py 250725\checkpoints\checkpoint_epoch_200.pth
move enc_in.f32 src_v2\ & move enc_ref.f32 src_v2\ & move enc_meta.txt src_v2\
（src_v2）del *.obj test_enc_v2.exe
build_test_enc_v2.bat
```
無言終了したら手打ち:
```
cl /nologo /MD /utf-8 /Od /I. /I..\..\dr-radev2\opus\dnn /I..\..\dr-radev2\opus\include /I..\..\dr-radev2\opus\celt /I..\..\dr-radev2\opus rade_enc_v2.c test_enc_v2.c rade_enc_v2_data.c ..\..\dr-radev2\opus\build\opus.lib /Fe:test_enc_v2.exe
test_enc_v2.exe
```

### 2. 送信全体
```
（radaeルート）copy /y src_v2\gen_tx_ref.py .
python gen_tx_ref.py 250725\checkpoints\checkpoint_epoch_200.pth
move tx_winv.f32 src_v2\ & move tx_in.f32 src_v2\ & move tx_ref.f32 src_v2\ & move tx_eoo.f32 src_v2\ & move tx_meta.txt src_v2\
（src_v2）del *.obj test_tx_v2.exe
cl /nologo /MD /utf-8 /Od /I. /I..\..\dr-radev2\opus\dnn /I..\..\dr-radev2\opus\include /I..\..\dr-radev2\opus\celt /I..\..\dr-radev2\opus rade_tx_v2.c rade_enc_v2.c test_tx_v2.c rade_enc_v2_data.c ..\..\dr-radev2\opus\build\opus.lib /Fe:test_tx_v2.exe
test_tx_v2.exe
```

## 判定
- encoder単体: 8ステップ全部 z(latent) が一致すれば確定
- 送信全体: 8ステップ全部 tx(IQサンプル)が一致すれば確定
- decoderと同じ知見(clamp)を最初から適用済みなので、初回で高精度一致
  する見込み(decoderのような5往復は起きないはず)

## 注意
- eoo()はmodel.eoo_v2を返すだけの定数(NN不要)。tx_eoo.f32に保存済みだが
  C側の対応関数はまだ未実装(rtx_eoo()を足すなら model.eoo_v2 相当の
  定数配列をexportから取得する必要がある。現状は転記のみで十分)。
- Winvは(Nc,M)複素、Wfwd(extract用, M,Nc)とは向きが逆なので注意。
