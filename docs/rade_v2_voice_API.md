# rade_v2_voice.dll — rv_ インターフェース API リファレンス

`rade_v2_voice.dll` は、RadeCallTest が使用してきた `rade_voice.dll`（GPL）を pure-C + FARGAN（BSD）で置き換えるライブラリです。`dumpbin /exports rade_voice.dll` で確認した 19 関数と同一シグネチャ（すべて `__cdecl`）を提供し、`RadeNative.cs` の P/Invoke 宣言と互換です。

- **正本ソース**: `rade_v2_voice.c`（2026-07-14 診断ログ除去版）
- **対応プロトコル**: V1（DR-NOPY）および V2。ハンドル生成時の `v2` 引数で選択します。
- **ライセンス**: BSD 系コンポーネント（radae_nopy 派生 C 実装、opus 内蔵 LPCNet/FARGAN、KISS FFT）のみで構成。

## 1. データ形式と単位

| 項目 | 形式 |
|---|---|
| モデム信号（`modem8k` / `modem8kOut`） | 実信号、`float`、サンプリング 8 kHz |
| 音声信号（`speech16k` / `speech16kOut`） | `float`（±1.0 想定）、サンプリング 16 kHz |
| ハンドル | `void*`（RX 用と TX 用は別型。相互に渡してはならない） |
| コールサイン | ASCII、最大 `RADE_EOO_CALLSIGN_MAX`（= 8）文字 + NUL |

サンプル数を表す引数（`n8`、`n16`、`maxOut`）はいずれも **サンプル数**（バイト数ではない）です。

## 2. ライフサイクルと呼び出し契約

```
rv_global_init()                 … プロセスで 1 回（他の rv_ 関数より先）
    rv_rx_create(v2) / rv_tx_create(v2)
        （オーディオコールバックごとに）
        rv_tx_push_speech() / rv_rx_push_modem()
        状態取得: rv_rx_synced() など
    rv_rx_destroy() / rv_tx_destroy()
rv_global_shutdown()             … プロセス終了時に 1 回
```

- `rv_global_init()` は V1（`rade_api.h`）のプロセス全体初期化 `rade_initialize()` を内包します。ハンドルごとに呼ぶのではなく、**アプリ起動時に 1 回**呼びます（既存 `rade_voice.dll` と同じ契約）。多重呼び出しは内部で防御されており実害はありません。
- V2 のみ使用する場合でも `rv_global_init()` を呼ぶ運用を推奨します（dual-watch 等で V1 ハンドルを併用する構成が前提のため）。
- ハンドルは内部にロックを持ちません。**同一ハンドルへの呼び出しは単一スレッドに直列化**してください（オーディオコールバックスレッドと UI スレッドの競合は実クラッシュの原因になり得ます）。

## 3. グローバル関数

### `void rv_global_init(void)`
プロセス全体の初期化。V1 の `rade_initialize()` を 1 回だけ実行します。すべての `rv_*` 関数より先に呼びます。

### `void rv_global_shutdown(void)`
プロセス全体の終了処理。V1 の `rade_finalize()` を実行します。全ハンドル破棄後に呼びます。

## 4. RX（受信）API

### `void* rv_rx_create(int v2)`
受信ハンドルを生成します。

| 引数 | 意味 |
|---|---|
| `v2` | `1` = V2、`0` = V1（DR-NOPY） |

**戻り値**: ハンドル。失敗時 `NULL`（メモリ不足、コンテキスト生成失敗、または V1 の features/EOO サイズ前提が崩れている場合）。

生成時に FARGAN（モデル重みは opus.lib 埋め込み）とヒルベルト変換の内部状態を初期化します。ゲイン初期値は `1.0`。

### `void rv_rx_destroy(void* h)`
受信ハンドルを破棄します。`NULL` 許容。

### `void rv_rx_set_gain(void* h, float gain)`
入力ゲインを設定します。ゲインは `rv_rx_push_modem` の入力サンプルに対して、ヒルベルト変換の入口で乗算されます。

### `int rv_rx_push_modem(void* h, float* modem8k, int n8, float* speech16kOut, int maxOut)`
受信の中核。8 kHz 実信号を投入し、復調・音声合成された 16 kHz 音声を受け取ります。

| 引数 | 意味 |
|---|---|
| `modem8k` | 入力: 8 kHz 実信号 |
| `n8` | 入力サンプル数 |
| `speech16kOut` | 出力バッファ: 16 kHz 音声 |
| `maxOut` | 出力バッファ容量（サンプル数） |

**戻り値**: `speech16kOut` に書き込んだサンプル数（`0` あり）。

内部処理は「ヒルベルト変換（実→IQ）→ RADE 復調 → FARGAN 音声合成」の 3 段です。入力は内部の IQ 中継バッファ（容量 4096 複素サンプル）に蓄積され、復調器が要求するサンプル数（V2: `nin`、V1: `rade_nin()`）が揃うたびに処理されます。呼び出し粒度と復調粒度は独立なので、**1 回の呼び出しで音声が出ないことは正常**です。

注意点:

- **FARGAN priming**: 同期確立直後の先頭 5 フレームは音声化せず priming に使います（`lpcnet_demo` と同じ作法）。idle→sync 遷移を検知するたびに priming をやり直すため、再同期直後は約 50 ms 音声が出ません。
- **バッファ溢れ**: IQ 中継バッファが満杯の場合、超過分は黙って破棄されます（通常のオーディオコールバック粒度では発生しない想定）。
- 出力が `maxOut` を超える分は破棄されます。`maxOut` には余裕を持たせてください（目安: `n8 × 2` 以上）。

### `int rv_rx_synced(void* h)`
同期状態を返します。`1` = 同期中、`0` = 非同期。直近の `rv_rx_push_modem` 内で復調器から取得した値のキャッシュです。

### `int rv_rx_snr_db(void* h)`
推定 SNR（dB、整数）を返します。**V1 のみ有効**（`rade_snrdB_3k_est()` のキャッシュ）。V2 の復調器は SNR を出力しないため常に `0` を返します。

### `float rv_rx_freq_offset(void* h)`
推定周波数オフセット（Hz）を返します。**V1 のみ有効**。V2 では常に `0.0` を返します。

### `int rv_rx_get_callsign(void* h, unsigned char* dst, int max)`
EOO で受信したコールサイン文字列を取得します。

**戻り値**: コピーした文字数（NUL 終端は別途付与）。未受信・V2・エラー時は `0`（このとき `dst[0] = 0` にクリアされます）。

**V1 のみ有効**です。V2 の EOO は固定波形でテキストチャネルを持たないため常に空を返します。なお V1 のコールサインチャネルは raw 7bit ASCII の簡易実装（LDPC/CRC なし）で、公式 FreeDV 互換の krl_eoo 方式とは**別物**です。RadeCallTest 実装同士の通信を想定しています。

### `int rv_rx_callsign_seq(void* h)`
コールサイン受信のシーケンス番号を返します。新しいコールサインをデコードするたびにインクリメントされるため、ポーリング側は前回値との比較で「新着」を検知できます。

### `int rv_rx_protocol(void* h)`
ハンドルのプロトコルを返します。`1` = V2、`0` = V1、`-1` = 無効ハンドル。

## 5. TX（送信）API

### `void* rv_tx_create(int v2)`
送信ハンドルを生成します。引数・失敗条件は `rv_rx_create` と同様。生成時に LPCNet 特徴抽出器（`lpcnet_encoder_create()`）を確保します。

### `void rv_tx_destroy(void* h)`
送信ハンドルを破棄します。`NULL` 許容。

### `void rv_tx_set_gain(void* h, float gain)`
入力ゲインを設定します。`rv_tx_push_speech` の入力音声に対して中継バッファ投入時に乗算されます。

### `void rv_tx_set_callsign(void* h, const char* callsign)`
EOO で送信するコールサインを設定します（最大 8 文字、超過分は切り捨て）。`NULL` を渡すとクリアされます。

- **V1**: `rade_tx_set_eoo_callsign()` へ即時反映され、次回の `rv_tx_eoo()` で送信されます。
- **V2**: EOO にテキストチャネルがないため保存のみで送信には反映されません（krl_eoo 等の実装は未着手）。

### `int rv_tx_protocol(void* h)`
`rv_rx_protocol` と同じ。`1` = V2、`0` = V1、`-1` = 無効ハンドル。

### `int rv_tx_push_speech(void* h, float* speech16k, int n16, float* modem8kOut, int maxOut)`
送信の中核。16 kHz 音声を投入し、変調済み 8 kHz 実信号を受け取ります。

| 引数 | 意味 |
|---|---|
| `speech16k` | 入力: 16 kHz 音声（±1.0 想定。範囲外は int16 変換時にクリップ） |
| `n16` | 入力サンプル数 |
| `modem8kOut` | 出力バッファ: 8 kHz 実信号 |
| `maxOut` | 出力バッファ容量（サンプル数） |

**戻り値**: `modem8kOut` に書き込んだサンプル数（`0` あり）。

内部処理は「LPCNet 特徴抽出（160 サンプル = 10 ms 単位）→ 4 サブフレーム蓄積（V2 の場合。40 ms）→ RADE 変調 → 複素 IQ の実部取り出し」です。RX 同様、呼び出し粒度と変調粒度は独立で、出力 `0` は正常です。

- V2 の特徴量は 21 要素/フレーム（LPCNet の先頭 20 要素 + auxdata 固定値 `-1`）。
- 音声中継バッファは 4096 サンプル。溢れた分は破棄されます。

### `int rv_tx_eoo(void* h, float* modem8kOut, int maxOut)`
送信終了（End Of Over）波形を出力します。PTT を離すタイミングで 1 回呼び、戻り値のサンプル数分を送信し切ってからキャリアを落とします。

**戻り値**: 書き込んだサンプル数。V1 では `rv_tx_set_callsign` 済みならコールサインが波形に載ります。V2 は固定波形です。

## 6. V1 / V2 機能差一覧

| 機能 | V1（DR-NOPY） | V2 |
|---|---|---|
| 音声送受信 | ○ | ○ |
| `rv_rx_synced` | ○ | ○（sig_det ベース） |
| `rv_rx_snr_db` | ○ | ×（常に 0） |
| `rv_rx_freq_offset` | ○ | ×（常に 0.0） |
| EOO コールサイン送信 | ○（簡易チャネル） | ×（固定波形） |
| EOO コールサイン受信 | ○ | ×（常に空） |

dual-watch 構成（V1/V2 ハンドル並行受信）では、SNR 表示等は V1 側の値で補えます。

## 7. 使用例（TX→RX ループバック）

`test_tx_rx_loopback.c` の骨子です。

```c
rv_global_init();

void *tx = rv_tx_create(1);   /* V2 */
void *rx = rv_rx_create(1);
rv_tx_set_gain(tx, 1.0f);
rv_rx_set_gain(rx, 1.0f);

float in_buf[960], modem_buf[8192], speech_buf[16384];
while (/* 入力音声がある間 */) {
    int n_modem = rv_tx_push_speech(tx, in_buf, 960, modem_buf, 8192);
    if (n_modem > 0) {
        int n_speech = rv_rx_push_modem(rx, modem_buf, n_modem,
                                        speech_buf, 16384);
        /* n_speech サンプルを再生/保存 */
    }
}

/* 送信終了: EOO を送って受信側の終端処理を確認 */
float eoo_modem[4096];
int n_eoo = rv_tx_eoo(tx, eoo_modem, 4096);
if (n_eoo > 0)
    rv_rx_push_modem(rx, eoo_modem, n_eoo, speech_buf, 16384);

rv_tx_destroy(tx);
rv_rx_destroy(rx);
rv_global_shutdown();
```

## 8. ビルド（Windows / MSVC）

`build_rade_v2_voice.bat` を使用します。前提:

- `opus.lib`（`scripts/build_opus.bat` でビルド。FARGAN モデルデータ埋め込み済み）
- V2 ソース一式（`src_v2/`）と学習済み重み `*_data.c`
- V1 ソース一式（`src_v1/` — radae_nopy の `src/` 相当。`rade_api_nopy.c` 系を使用）
- `ht_coeff.h`（ヒルベルト変換係数）、`rade_v2_constants_data.c`

環境変数 `OPUS_SRC` / `OPUS_LIB` / `V1_SRC` でパスを上書きできます。ビルド後、`dumpbin /exports rade_v2_voice.dll | findstr rv_` で 19 関数のエクスポートを確認してください。

## 9. 既知の制約・注意事項

- 同一ハンドルに対する呼び出しはスレッドセーフではありません。呼び出し側で直列化してください。
- IQ / 音声中継バッファは固定長 4096 で、溢れは無警告で破棄されます。オーディオコールバック 1 回あたりの投入量は数百〜千サンプル程度を想定しています。
- 再同期のたびに FARGAN priming（5 フレーム ≒ 50 ms）が入り、その間は無音です。
- V2 の SNR / 周波数オフセット / コールサインは復調器側に取得手段がないための制約であり、DLL 側のバグではありません。
- V1 の TX 特徴量レイアウト（LPCNet 36 要素をそのまま入力）は動作確認済みですが、上流ソースとの突き合わせによる仕様確認は継続課題です。
