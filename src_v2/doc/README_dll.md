# rade_api_v2 (DLL化) 手順

RADE V2 の全8部品(受信6+送信2)を束ねた公開API。Python/torch/conda不要、
opus静的リンクの単体DLLとして配布できる。

## 新しい成果物
- rade_api_v2.h / .c ── 公開API本体。opaqueな RADEV2Context に全状態を
  持たせ、rade_v2_open/close/tx/rx の薄いインターフェースを提供。
- export_rade_v2_constants.py ── Wfwd/Winv/pend/eoo_v2 を C 定数配列として
  書き出す新しいスクリプト。**重要な設計ポイント**: これまでの
  gen_*_ref.py は毎回 Python から Wfwd 等を読んで検証用 .f32 に書き出して
  いたが、DLLは実行時にPythonが無いため、これらを**コンパイル時に埋め込む
  定数**として恒久化する必要があった。export_rade_v2_weights.py(NN重み)
  とは別に、この定数エクスポートが新規に必要だった。
- test_api_v2.c ── API経由での統合テスト(rx/tx両方、既存のsync_*.f32と
  tx_*.f32を再利用)。
- build_rade_v2_dll.bat ── rade_v2.dll をビルド。
- build_test_api_v2.bat ── API経由の検証EXEをビルド・実行。

## 手順

### 1. 定数(Wfwd/Winv/pend/eoo)を生成(初回のみ)
```
cd C:\dev\rade_v2_voice
copy /y src_v2\export_rade_v2_constants.py .
python export_rade_v2_constants.py 250725\checkpoints\checkpoint_epoch_200.pth src_v2
```
→ src_v2\rade_v2_constants_data.c/.h が生成される。

### 2. API経由の統合テスト(まずこちらで動作確認)
既存の sync_*.f32 / tx_*.f32(前回のセッションで生成済みのはず。無ければ
gen_sync_ref.py / gen_tx_ref.py を再実行)がsrc_v2にある状態で:
```
cd src_v2
build_test_api_v2.bat
```
期待結果:
```
=== rade_v2_rx (via API) 検証 ===
state不一致=0 have不一致=0
=== rx API 一致 成功 ===
=== rade_v2_tx (via API) 検証 ===
最大相対誤差=x.xxxe-0x
=== tx API 一致 成功 ===
=== rade_api_v2 全体 成功 ===
```
これが通れば、API層(rade_v2_open/rx/tx)が個別部品を正しく束ねている
ことが確認できる(個別部品自体は既に確定済みなので、ここでのバグは
配線ミスの可能性が高い)。

### 3. DLL本体をビルド
```
build_rade_v2_dll.bat
```
→ rade_v2.dll が生成される。これがRadeCallTest等から呼び出す最終成果物。

## 公開API概要(rade_api_v2.h)
```c
RADEV2Context* rade_v2_open(void);
void rade_v2_close(RADEV2Context *ctx);

int rade_v2_n_features_in(RADEV2Context *ctx);   // 送信入力サイズ(84)
int rade_v2_n_tx_out(RADEV2Context *ctx);        // 送信出力サンプル数(Ns*sym_len)
int rade_v2_n_features_out(RADEV2Context *ctx);  // 受信出力サイズ(84)
int rade_v2_sym_len(RADEV2Context *ctx);         // 1シンボル長(Ncp+M)
int rade_v2_n_eoo_out(RADEV2Context *ctx);       // EOO送信サンプル数

void rade_v2_tx(RADEV2Context *ctx, const float *features_in, RADE_COMP *tx_out);
void rade_v2_tx_eoo(RADEV2Context *ctx, RADE_COMP *eoo_out);

int rade_v2_rx(RADEV2Context *ctx, const RADE_COMP *rx_in, int *nin_io,
              float *features_out, int *has_features_out,
              int *sig_det_out, int *sine_det_out);
void rade_v2_rx_reset(RADEV2Context *ctx);
```

使い方の骨格(C#側の実装イメージ):
```csharp
var ctx = rade_v2_open();
int nin = rade_v2_sym_len(ctx);
// 受信ループ: nin個のIQサンプルを rade_v2_rx に渡す。nin_ioは呼び出し後
// 更新されるので、次のバッファサイズに使う。has_features==1のときのみ
// features_out(84要素)を後段(FARGAN等)に渡す。
```

## 未実装/要確認事項
- rade_v2_tx_eoo は model.eoo_v2 をそのまま返す実装。EOO送信の呼び出し
  タイミング(通常のtransmit_frameを何回呼んだ後に送るか)はradae_txe.py
  等の上位ロジックを見て、呼び出し側(C#やRadeCallTest側)で制御する。
- BPF(SSBフィルタ)は rade_tx_v2.c に未実装。V1のTXバンドパスフィルタ
  (400-2600Hz/241taps等)をV2用に移植するかは、Ham Fair要件次第で検討。
- W_first/w_last(SNR推定・BPF帯域計算用)は rade_api_v2.c 内にハードコード
  (0.834486, 1.472622)。モデルが変わった場合は要更新
  (export_rade_v2_constants.py で model.w[0]/model.w[Nc-1] も出力する
  よう拡張するのが望ましい)。

## DLLをC#から使う際の注意
- 呼び出し規約は cdecl(MSVCデフォルト)。DllImportで
  `[DllImport("rade_v2.dll", CallingConvention = CallingConvention.Cdecl)]`
  を指定すること(V1のRadeNative.csと同様のパターンを踏襲できるはず)。
- RADE_COMP は `{float real; float imag;}` の8バイト構造体。C#側は
  `[StructLayout(LayoutKind.Sequential)] struct RadeComp { float real, imag; }`
  に対応させる。
