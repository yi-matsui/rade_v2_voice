# RADE V2 本家既定仕様パッチ 適用手順(2026-07-09)

実ソースレビューの結果を反映したパッチ済みファイル3点と、その適用手順。

## 納品ファイル(全文差し替え)
| ファイル | 変更内容 |
|---|---|
| rade_extract_v2.c | **time_offset=-16 / correct_time_offset=-8 を実装(既定値)**。w[c]をWfwdから自己導出。rext_set_time_offset() 追加 |
| rade_sync_v2.c | **limit_pitch(既定ON)/ mute(既定OFF)を有効化**(コメントアウトされていた骨格を本実装化) |
| rade_api_v2.c | **RX BPF統合(既定ON)**、limit_pitch/mute既定設定、**timing_adjをrx2.py同様に有効化**、setter 6種追加、close時のreoo_freeリーク修正、rx_resetの拡充(extract/BPF/EOO平滑もクリア) |

前回納品分(rade_bpf_v2.c/.h, gen_bpf_ref.py, test_bpf_v2.c,
build_test_bpf_v2.bat, run_rx2_det.py, plot_pitch_diff.py)と併せて使う。

---

## 実ソースレビューで確定した事項(重要)

### ★最重要: rade_extract_v2.c の time_offset 欠落(値の継承ミス第5号)
旧ソース冒頭コメントに「V2: time_offset=0, correct_time_offset=False」と
あったが、これは誤り。**本家rx2.pyの既定は -16 / -8** であり、DFT窓を
CPの中央側へ16サンプル前倒しし(マルチパス/タイミング誤差への耐性)、
それが生むキャリア毎位相をexp(-j·ct·w[c])で補正する設計。
- 一次ソース: rx2.py argparse 既定値、radae/radae.py 474行(窓)・
  486-495行(位相補正)、クリス氏 radc_const_v2.h(-16/-8で実装済み)
- 影響: 旧C実装(0,0)はクリーンなループバックでは自己整合するが、
  **rx2.pyと数値照合すると系統的にズレる**(7/9に観測した残差0.1〜0.24の
  一部を説明しうる)。また実チャネル(OTA/Ham Fair)ではタイミング余裕が
  無く、ISI耐性が本家より低い。
- 既存の extract/sync 基準(.f32)は (0,0) のPythonで生成されているはず
  なので、下の回帰対応表に従うこと。

### decoder / encoder は本家に忠実(確定)
- rade_dec_v2.c(clamp版)のclamp配置は Python `n(glu(n(gru)))` /
  `n(conv)` / `n(tanh(dense1))` と正確に一致。GRU stateの無clamp持ち越しも
  `GRUStatefull.states` と一致。**復元推奨(計画通り)**。noclamp版とは
  数値等価(clampは値域的に不発)なのでどちらでも音は変わらないが、
  本家忠実性の観点でclamp版を正とする。
- rade_enc_v2.c も CoreEncoderStatefull.forward(n(gru), n(conv)、GLU無し、
  z_denseにn()無し)と一致。bottleneck=0修正済み。
- rade_frame_sync.c は models_sync.py と一致。問題なし。
- rade_eoo_v2.c は float kiss_fft(本家np.fft/クリス氏はdouble)だが
  検証済み誤差5.1e-07であり実用上問題なし(記録のみ)。

### その他の発見
- rade_api_v2.c 旧版は close 時に reoo_free を呼んでおらず、
  kiss_fft cfg と作業バッファがリークしていた(パッチで修正)。
- timing_adj はどこからも有効化されておらず常時OFFだった。rx2.pyは
  `s > timing_adj_at(既定0)` で実効的に最初から有効。パッチで同挙動に。
- AGC: C既定ON(実運用向き、クリス氏と同判断)だが本家rx2.py既定はOFF。
  Python照合時は必ず条件を揃える(下表)。

---

## ヘッダ側の追記(3ファイル、手作業)

### rade_extract_v2.h ── struct rade_extract_v2_state に追加
```c
    /* 本家 rx2.py 既定: -16 / -8(radae.py receiver 474/486行) */
    int       time_offset;
    int       correct_time_offset;
    float     w[32];          /* キャリア角周波数(Wfwdから導出, Nc<=32) */
    RADE_COMP ct_phase[32];   /* exp(-j*ct*w[c]) 事前計算 */
```
プロトタイプ追加:
```c
void rext_set_time_offset(rade_extract_v2_state *st,
                          int time_offset, int correct_time_offset);
```

### rade_sync_v2.h ── rade_sync_v2_ctx に追加
```c
    int limit_pitch;   /* 本家既定ON: feat[18] を -1.4 で下限クリップ */
    int mute;          /* 本家既定OFF: 信号喪失時 feat[0] = -5 */
```

### rade_api_v2.h ── 公開関数追加(既存のexportマクロを付けること)
```c
void rade_v2_set_limit_pitch(RADEV2Context *ctx, int enable);   /* 既定1 */
void rade_v2_set_mute(RADEV2Context *ctx, int enable);          /* 既定0 */
void rade_v2_set_bpf(RADEV2Context *ctx, int enable);           /* 既定1 */
void rade_v2_set_agc(RADEV2Context *ctx, int enable);           /* 既定1 */
void rade_v2_set_timing_adj(RADEV2Context *ctx, int enable);    /* 既定1 */
void rade_v2_set_time_offset(RADEV2Context *ctx,
                             int time_offset, int correct_time_offset);
                                                    /* 既定 -16, -8 */
```
C#側(RadeV2Native.cs)にも対応するDllImportを6本追加する。

### ビルドスクリプト
build_rade_v2_dll.bat / build_test_api_v2*.bat のソース一覧に
`rade_bpf_v2.c` を追加。

---

## 回帰対応表(どの設定でどの基準と照合するか)

| 照合相手 | C側の設定 | Python側の引数 |
|---|---|---|
| **既存の .f32 基準**(旧世代、まず回帰確認) | set_time_offset(0,0), set_limit_pitch(0), set_bpf(0), set_timing_adj(0), set_agc(基準生成時と同じ) | (基準生成時のまま) |
| **rx2.py 既定仕様**(今後の正) | すべて既定のまま(ただしagcは条件を揃える) | `--nolimit_pitch`等を**付けない**。C側agc=1なら `--agc` を付ける、またはC側 set_agc(0) |
| **決定論比較**(run_rx2_det.py) | 上と同じ+条件表明 | run_rx2_det.py に同上の引数 |

手順の推奨順序:
1. **回帰**: パッチ適用後、旧設定(表1行目)に切り替えて既存の
   test_extract/test_sync/test_api が従来精度で通ることを確認
   (=パッチが旧動作を完全に再現できることの確認)
2. **新基準生成**: gen_extract_ref.py / gen_sync_ref.py の RADAE 構築に
   `time_offset=-16, correct_time_offset=-8` を渡して基準を再生成し、
   C既定(表2行目)で一致確認
3. **BPF単体**: build_test_bpf_v2.bat(前回納品、検証手順は
   README_bpf_limit_pitch.md 参照)
4. **一気通貫**: run_rx2_det.py(--no_bpf等を外した完全条件一致)vs
   C(既定)で49.76秒英語音声を plot_pitch_diff.py 比較。
   **time_offset実装により、7/9時点の残差0.1〜0.24がどこまで縮むかが
   今回最大の見どころ**
5. 聴感: 同じ出力を fargan-synthesis して、ざらつきの変化を確認

## 補足
- rade_v2_rx_reset は extract(rx_phase, rx_i)・BPF・EOO平滑も
  クリアするようになった。time_offset設定はreset後も引き継がれる。
- rext_extract の rx_sym_td(EOO検出用)には time_offset を適用しない
  (本家 _extract_symbol と同じ。DFT窓の前倒しはdemapのみ)。
- w[c] は Wfwd[1*Nc+c] の偏角から導出するため、checkpoint を差し替えて
  rade_v2_constants_data.c を再生成すれば自動で追従する(再export時の
  更新漏れが起きない設計)。


---

## 追補(2026-07-09 rev2): 実ソース精査で追加発見した2件

### ★実バグ: EOO平滑値のリセットが効いていない(rade_sync_v2.c で修正)
本家 radae_v2.py の `eoo_smooth` は Receiver の単一メンバ(101-102行定義)で、
`_detect_eoo`(278行)が更新し、`_process_idle` のsync遷移(193行)と
`_process_sync` のEOO検出(241-242行)がリセットする。

C 実装では `eoo_smooth` が **`rade_rx_v2_state` と `rade_eoo_v2_state` の
両方に存在**し、リセット(`rx->eoo_smooth = 0.0f`)は前者にだけ書かれていた。
しかし `reoo_detect()` が更新・判定に使うのは後者。つまり
**EOO平滑値が一度も 0 に戻らない**。

- 症状: EOO検出でidleに落ちた直後、`eoo.eoo_smooth` が閾値(TEOO)付近に
  残るため、再syncした瞬間にまた誤EOO検出してidleへ戻る。受信が始まらない、
  または断続する。**連続交信・ロング運用でのみ顕在化**する種類の不具合で、
  短いユニットテスト(EOOを1回しか通さない)では絶対に出ない。
- 修正: `reset_eoo_state(ctx)` を新設し、`ctx->eoo->eoo_smooth/eoo_corr` を
  リセット。`rx->eoo_smooth` は状態表示用ミラーに降格(毎シンボル値を写す)。
  `_process_idle` の sync 遷移時のリセットも `rsync_process_symbol` 側で実施
  (`rrx_process_idle` は eoo 部品を知らないため)。

### rrx_init の未初期化フィールド(rade_rx_v2.c で修正)
`hangover` / `eoo_smooth` / `eoo_count` が `rrx_init` で初期化されていなかった。
`rade_v2_open()` では calloc により偶然ゼロだったので露見しなかったが、
`rade_v2_rx_reset()` は `rrx_init` を呼び直すだけなので、**再同期のたびに
前セッションの値が残留**していた。`hangover` も呼び出し側が毎回 75 を代入して
補っていたので、既定値を `rrx_init` に集約した(呼び出し側の代入は残っていても
無害)。

### rade_tx_v2.c
bottleneck=0 の根拠(tx2.py / クリス氏コメント)をファイル冒頭に明記し、
誤った旧呼び出し(`bottleneck=1`)のコメントアウト行を削除。動作は変更なし。

### rade_dec_v2.c / rade_enc_v2.c
**変更不要**。clamp 版の `rade_dec_v2.c`(アップロードされたもの)が本家忠実で
あり、これを正とする。`rade_dec_v2_noclamp.c` は退避用として残してよいが、
ビルド対象から外すこと。両者は数値等価(clamp は値域的に不発)なので、
差し替えても音質は変わらない ── これは「clamp復元後の再テスト」の予測でもある。

### 差し替えファイル(rev2 時点の最終形)
| ファイル | 状態 |
|---|---|
| rade_extract_v2.c | 差し替え(time_offset=-16 / correct_time_offset=-8 実装) |
| rade_sync_v2.c | 差し替え(limit_pitch/mute 実装 + **EOOリセット修正**) |
| rade_api_v2.c | 差し替え(BPF統合、timing_adj、setter×6、reoo_freeリーク修正) |
| rade_rx_v2.c | 差し替え(**rrx_init 未初期化修正**) |
| rade_tx_v2.c | 差し替え(コメント整理のみ、動作不変) |
| rade_bpf_v2.c/.h | 新規追加(ビルド一覧にも追加すること) |
| rade_dec_v2.c | **変更不要**(clamp版を正とする) |
| rade_enc_v2.c | **変更不要** |
| rade_frame_sync.c / rade_eoo_v2.c | **変更不要** |
