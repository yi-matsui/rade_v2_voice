> **注記**: これは本リポジトリの `rade_v2_voice.dll`(pure-C、Python非依存)とは別物、
> upstream `drowe67/radae` の Python 埋め込み版 C API (`rade.dll`) を MSVC で
> ビルドするための手順書です。opus のビルド方法(コミット固定・CMakeオプション・
> `opus-nnet.h.diff` を適用しない理由)はこのリポジトリの `scripts/build_opus.bat`
> と共通の根拠に基づいています。参考資料として収録しています。

# dr-radev2 ブランチ C API (rade.dll) MSVC ビルド手順

対象: `drowe67/radae` dr-radev2 ブランチ (commit `ad892b91`)

## 重要な前提認識

このブランチの `src/rade_api.c` は **Python 埋め込み版** です(radae_nopy 系のピュアC実装とは別物)。

- OFDM モデム部は `radae_txe.py` / `radae_rxe.py` を CPython 経由で実行
- `RADE_USE_C_ENCODER` / `RADE_USE_C_DECODER` フラグ指定時のみ、コアの
  エンコーダ/デコーダ(NN部)が C 実装(`rade_enc.c` / `rade_dec.c`)で動作
- したがって **rade.dll の実行時に Python + numpy + torch + matplotlib が必須**
  (`radae_txe.py` は torch を、`radae_rxe.py` は torch と matplotlib を
  モジュールレベルで import している)
- `rade_open()` の `model_file` 引数は現状無視される(TODO のまま)。
  実際のモデルは Python 側デフォルトの
  `model19_check3/checkpoints/checkpoint_epoch_100.pth` がロードされる。
  `radae_rxe.py` の `radae_rx.__init__` はデフォルト `v=2`。

## ビルド手順

VS の「x64 Native Tools Command Prompt」で以下を順に実行:

```
rem 1. ヘッダのパッチ (RADE_EXPORT の __stdcall 除去)
python patch_msvc.py <radaeリポジトリのルート>

rem 2. opus (FARGAN/DRED/OSCE 有効) の静的ビルド
build_opus.bat

rem 3. rade.dll のビルド
set RADAE_ROOT=<radaeリポジトリのルート>
build_rade.bat
```

## 各パッチ/設定の根拠

| 項目 | 内容 |
|---|---|
| `rade_api.h` | `__declspec(dllexport) __stdcall` の並びは MSVC では構文エラー(GCC のみ許容)。`__stdcall` を除去し cdecl とする。C# P/Invoke は `CallingConvention.Cdecl` を指定 |
| `rade_api.c` の VLA | `rade_tx()`/`rade_rx()` 内の可変長配列は MSVC 非対応。`RADE_VLA` マクロ(`_alloca`)で対策済みであることを patch_msvc.py が確認する。`rade_enc.c`/`rade_dec.c` のバッファはコンパイル時定数なので問題なし |
| opus のコミット | `cmake/BuildOpus.cmake` のピン留め `940d4e5a` を使用。autotools の `--enable-osce --enable-dred` に相当する CMake オプションは `-DOPUS_DEEP_PLC=ON -DOPUS_DRED=ON -DOPUS_OSCE=ON` |
| `opus-nnet.h.diff` | nnet.h の compute_* 群に `__attribute__((visibility))` を付ける GCC 前提のパッチ。opus を rade.dll に静的リンクする MSVC 構成では不要のため **適用しない** |
| `/bigobj /Od` | `rade_enc_data.c` / `rade_dec_data.c` は各24MBの重みデータ。/O2 だとコンパイルが極端に遅くなるため /Od、セクション数超過対策に /bigobj |
| `/DOPUS_BUILD` | opus 内部ヘッダ(celt/arch.h 系)のガード対策 |
| `/utf-8` | ソース内の日本語コメント(UTF-8)を CP932 と誤解釈させないため |
| python ライブラリ | `pyconfig.h` の `#pragma comment(lib)` で自動リンクされるため `/LIBPATH:%PY_HOME%\libs` のみ指定 |

## 実行時のセットアップ

1. `python3xx.dll` を PATH に(rade.dll のロード時依存)
2. `PYTHONPATH` に radae リポジトリのルートを設定
   (`radae_txe` / `radae_rxe` モジュールと `radae/` パッケージの解決)
3. 作業ディレクトリに `model19_check3/checkpoints/checkpoint_epoch_100.pth`
   (相対パスでロードされるため)
4. `pip install numpy torch matplotlib` 済みの環境であること
5. venv 使用時は `Py_InitializeEx` が stdlib を見つけられるよう
   `PYTHONHOME` の設定に注意
6. 呼び出し順: `rade_initialize()` → `rade_open()` → (tx/rx) →
   `rade_close()` → `rade_finalize()`。
   `rade_initialize()` は GIL を解放した状態で返るため、
   以降の API はどのスレッドから呼んでも良い(内部で GILState を取得)

## 制約・注意

- `check_error()` は失敗時に `exit(1)` する(ライブラリとしては乱暴なので、
  ホストアプリ組み込み時はクラッシュ相当と考えること)
- `rade_tx_set_eoo_bits()` は stderr に `%ld` で npy_intp を出力しており
  x64 MSVC では警告が出るが実害なし
- radae_tx.c / radae_rx.c(テスト用 exe)はビルド対象外とした。
  必要なら同じ INCS/DEFS で cl に渡せばビルド可能
