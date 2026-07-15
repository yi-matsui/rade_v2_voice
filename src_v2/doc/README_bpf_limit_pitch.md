# RX BPF + limit_pitch 実装・検証手順(本家既定仕様への穴埋め)

作成日: 2026-07-09
方針: デバッグではなく「本家rx2.py既定仕様との機能完全一致」。
実装するガードレールは2つ: (1) limit_pitch (2) RX入力BPF。
どちらも本家rx2.pyの既定ONであり、クリス氏実装(Thetis-RADE)も既定ONで実装済み。

## 成果物一覧
- rade_bpf_v2.c / .h    ── 複素BPF(radae/dsp.py complex_bpf のC移植)
- gen_bpf_ref.py        ── BPF検証用Python基準生成(二重基準方式)
- test_bpf_v2.c         ── BPF検証EXE(ブロック処理で本家一括処理と照合)
- build_test_bpf_v2.bat ── 上記ビルド・実行
- run_rx2_det.py        ── 決定論Python(ディザ無し)実行ラッパ
- plot_pitch_diff.py    ── feat[18]等の時系列比較プロット(自動位置合わせ付き)

## 検証済み事項(サンドボックスで実施済み、お手元では確認のみ)
- rade_bpf_v2 のブロック処理(160サンプル単位)出力は、本家complex_bpfの
  一括処理と数値一致する(状態引き継ぎ込み)。
- 判定基準(倍精度位相版Python)に対し最大相対誤差 7.0e-06 で成功。
- 本家素のcomplex64版との差は末尾で最大1.4e-03あるが、これは**本家自身の
  位相テーブル(complex64)の角度量子化ジッタ**であることをPython同士の
  A/B(c64テーブル vs c128テーブル)で確認済み。C実装は「正確な側」に一致
  している。test_bpf_v2.exe は両方の数字を表示する。

---

## 1. limit_pitch の実装(アクション4)

### 1-1. 本家仕様(一次ソース)
- radae_v2.py `_update_frame_sync_and_decode`(decoder呼び出し直後):
  ```python
  if self.args.limit_pitch:
      features[:, :, 18].clamp_(min=-1.4)
  ```
- rx2.py 既定: `parser.set_defaults(limit_pitch=True)`。ヘルプ文言:
  "disable limiting (clip) lower end of pitch feature to prevent
  synthesis pops with some speakers/channels (default enabled)"
- クリス氏(radc_rx_v2.c): `rx->limit_pitch = 1;` 既定ON、decode直後に
  `features_out[f*21+18] < -1.4f` をクリップ。

### 1-2. 実装箇所
decoder(rade_core_decoder_v2)の出力が features_out に確定した直後、
つまり rade_sync_v2.c の _update_frame_sync_and_decode 相当の関数内、
winning判定後のdecode呼び出しの直後。API層(rade_api_v2.c)でも可だが、
本家と同じ層(受信状態機械側)に置く方が対応関係が明快。

### 1-3. 追加コード

状態構造体(rade_sync_v2.h の RADESyncV2Ctx 等)にフィールド追加:
```c
    int limit_pitch;   /* 本家既定ON: feat[18](ピッチ)を-1.4で下限クリップ */
```

初期化関数に(本家・クリス氏と同じく既定ON):
```c
    st->limit_pitch = 1;
```

decode直後(features確定直後)に:
```c
    /* radae_v2.py: features[:,:,18].clamp_(min=-1.4)
       合成ポップ(synthesis pops)防止。本家rx2.py既定ON */
    if (st->limit_pitch) {
        int f;
        for (f = 0; f < 4 /*frames_per_step*/; f++) {
            if (features_out[f * 21 + 18] < -1.4f)
                features_out[f * 21 + 18] = -1.4f;
        }
    }
```

API層(rade_api_v2.h/.c)に切替関数を追加(A/B検証用):
```c
/* limit_pitch(feat[18]の-1.4下限クリップ)のON/OFF。既定ON(本家準拠) */
void rade_v2_set_limit_pitch(RADEV2Context *ctx, int enable);
```

### 1-4. 検証
1. 短系列回帰: 既存の test_sync_v2 / test_api_v2 を再実行。
   **注意**: Python基準側が limit_pitch OFF で生成されている場合、
   ONにした C と食い違う可能性がある。既存基準と照合する時は
   rade_v2_set_limit_pitch(ctx, 0) で切ってから照合するか、
   基準を limit_pitch 込みで再生成する。
2. 発火カウント: plot_pitch_diff.py が feat[18] < -1.4 のフレーム数を
   C/Python両方について表示する。英語49.76秒素材で必ず確認すること
   (前回の「該当0件」は日本語9秒素材での結果)。
3. F0検証: スペクトル画像で観察された104Hz vs 95Hzの差が、
   feat[18]の時系列差として現れるか pitch_diff.png で確認。
   FARGANの変換式は period = round(256 / 2^(feat18+1.5))、
   F0 = 16000/period。feat18の差0.13が約1.5半音のF0差に相当する。

---

## 2. RX BPF の組み込み(アクション5)

### 2-1. 単体検証(まずこちら)
```
（radaeルート）copy /y src_v2\gen_bpf_ref.py .
python gen_bpf_ref.py
move bpf_in.f32 src_v2\ & move bpf_ref.f32 src_v2\ & move bpf_ref64.f32 src_v2\ & move bpf_meta.txt src_v2\
（src_v2）build_test_bpf_v2.bat
```
期待結果:
```
倍精度位相基準との最大相対誤差=7.0e-06程度  <- 判定対象
本家c64基準との最大相対誤差  =1.4e-03程度  <- 参考(正常)
=== bpf_v2 一致 成功 ===
```

### 2-2. rade_api_v2 への組み込み
本家rx2.pyは「受信ストリームの先頭で一括フィルタ」、クリス氏は
「rade_rx_v2_process の入口で毎ブロック適用」。ブロック処理でも数値
等価であることは単体検証で保証済みなので、クリス氏方式(入口適用)を採る。

RADEV2Context にフィールド追加:
```c
    rade_bpf_v2_state bpf;
    int bpf_en;        /* 本家rx2.py既定ON */
```

rade_v2_open() 内(w_first/w_last のハードコード値は既存のものを使用):
```c
    {
        /* rx2.py: bandwidth=1.2*(w[Nc-1]-w[0])*Fs/(2*pi),
                   centre=(w[Nc-1]+w[0])*Fs/(2*pi)/2, Ntap=101 */
        float fs = 8000.0f;
        float bw = 1.2f * (RADE_V2_W_LAST - RADE_V2_W_FIRST) * fs / (2.0f * (float)M_PI);
        float fc = (RADE_V2_W_LAST + RADE_V2_W_FIRST) * fs / (2.0f * (float)M_PI) / 2.0f;
        rbpf_init(&ctx->bpf, 101, fs, bw, fc);
        ctx->bpf_en = 1;
    }
```
(w_first=0.834486, w_last=1.472622。checkpoint変更時は
 export_rade_v2_constants.py 拡張で model.w[0]/model.w[Nc-1] を出力し
 定数側を更新する ── README_dll.md 記載の既知課題と同一)

rade_v2_rx() の入口、rx_in を下位に渡す前に:
```c
    RADE_COMP filt[RADE_BPF_V2_MAXLEN];   /* nin最大200に対し256 */
    const RADE_COMP *in_s = rx_in;
    if (ctx->bpf_en) {
        rbpf_process(&ctx->bpf, filt, rx_in, *nin_io);
        in_s = filt;
    }
    /* 以降 in_s を使用 */
```

切替関数(A/B検証用):
```c
void rade_v2_set_bpf(RADEV2Context *ctx, int enable);   /* 既定ON */
```

DLLビルド(build_rade_v2_dll.bat)に rade_bpf_v2.c を追加すること。

### 2-3. 組み込み後の回帰
既存の sync_*.f32 基準は「BPF無しPython」で生成されている可能性が高い。
照合時は rade_v2_set_bpf(ctx, 0) で切るか、gen_sync_ref.py 側の入力に
同じBPFを適用して基準を再生成する(rx2.pyと同じ位置=receiverに入る前)。

---

## 3. 決定論比較環境(アクション2の準備)

### 3-1. Python側(radaeルートで)
```
copy /y src_v2\run_rx2_det.py .
python run_rx2_det.py 250725\checkpoints\checkpoint_epoch_200.pth rx.f32 features_hat_det.f32 ^
    --latent-dim 56 --frames_per_step 4 --rate_Fs --cp 0.004 --w1_dec 128 ^
    --auxdata --peak --no_bpf --nolimit_pitch
```
条件合わせの原則: **C側に実装した機能から順に、対応する --no_xxx を外す**。
- BPF組み込み前: --no_bpf 必須 / 組み込み後: 外す
- limit_pitch組み込み前: --nolimit_pitch 必須 / 組み込み後: 外す
- AGC: rx2.py既定OFF。C側 agc_en も 0 に(条件を明示的に一致させる)

### 3-2. 比較
```
python plot_pitch_diff.py features_hat36.f32 features_hat_det.f32
```
- フレーム位置合わせは feat[0] の相互相関で自動実施
- 出力: pitch_diff.png + 次元別統計 + feat[18]<-1.4 発火カウント

### 3-3. 判定基準(アクション2)
決定論Python(ディザ無し・条件一致)とC実装の平均絶対差が全次元で
~1e-3以下なら、decoder+受信チェーンの数値的無罪が確定。
それより大きい次元・区間が残れば、その座標(何秒地点・何次元)が
次の調査対象(time_offset等の定数照合へ)。

---

## 4. 実施順序(再掲)
1. limit_pitch 実装(セクション1)+ 発火カウント確認
2. BPF単体検証(セクション2-1)→ API組み込み(2-2)→ 回帰(2-3)
3. 決定論Python比較(セクション3)── C側の実装進捗に合わせて
   --no_bpf / --nolimit_pitch を外していく
4. ディザA/B聴感(run_rx2_det.py の出力 vs 素のrx2.py の出力を
   fargan-synthesis して聴き比べ)── 「ざらつき」の成分分解

## 5. 注意(いつものやつ)
- 再ビルド前は del *.obj を忘れない
- gen_bpf_ref.py / run_rx2_det.py は必ずradaeルートで実行
- rade_bpf_v2.c は M_PI ガードを自前で持つ(MSVC対応済み)
- .bat は純ASCII(build_test_bpf_v2.bat は準拠済み)
