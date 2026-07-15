# 実音声でのTX->RXループバックテスト 手順

ダミーのランダム特徴量ではなく、実際の音声ファイルから抽出した本物の
特徴量で RADE V2 の encoder->TX->RX->decoder を通し、再構成された
音声を実際に聞けるところまで確認する。

## 全体の流れ
```
音声(.wav) --lpcnet_demo--> 生特徴量(36/frame) --C#(内部変換)--> V2特徴量(21/frame)
  --Tx--> IQストリーム --Rx--> 復調featuresを収集
  --C#(36要素に復元)--> 復調特徴量(36/frame) --lpcnet_demo--> 音声(.s16) --sox--> .wav
```

## 前提
- opus の lpcnet_demo が使えること(ビルド済みのはず。build/src/lpcnet_demo)
- 用意した音声ファイルが 16kHz, モノラル の .s16(生PCM)であること。
  .wav しか無い場合は sox で変換:
  ```
  sox 入力.wav -t .s16 -r 16000 -c 1 入力.s16
  ```

## 手順

### 1. 音声から生特徴量を抽出(radaeルート or opusビルド済みディレクトリで)
```
lpcnet_demo -features 入力.s16 features_raw36.f32
```

### 2. C#側に features_raw36.f32 をコピー
```
copy features_raw36.f32 C:\dev\rade_v2_voice\src_v2\csharp\
```

### 3. ビルド(初回のみ、または更新時)
```
cd C:\dev\rade_v2_voice\src_v2\csharp
build_cs_smoketest.bat
```
(このバッチは引数無しで実行すると通常の疎通確認+ダミーループバックテストが
走る。今回は続けて下記コマンドを別途実行する)

### 4. 実音声ループバックテストを実行
```
RadeV2SmokeTest.exe features_raw36.f32 features_hat36.f32
```
出力例:
```
入力: 250 フレーム(2.50秒), 使用: 248 フレーム(62 modemフレーム)
TX: IQストリーム長 = 21760 サンプル
RX: sync状態=xx回、復調フレーム数=xxx
wrote features_hat36.f32 (xxx フレーム, 36要素/フレーム)
音声化: lpcnet_demo -fargan-synthesis features_hat36.f32 out.s16
wav変換: sox -t .s16 -r 16000 -c 1 out.s16 out.wav
```

### 5. 音声化して聞く
```
lpcnet_demo -fargan-synthesis features_hat36.f32 out.s16
sox -t .s16 -r 16000 -c 1 out.s16 out.wav
```
out.wav を再生して、元の音声とどの程度近いか確認する。

## 期待される結果と注意点
- **音素性は残るはず**: RADE V2 の encoder/decoder は学習済みペアなので、
  無雑音ループバックであれば、ピッチ・音韻の骨格は復元されるはず。
  ただし本番のRADEは元々ロッシー(不可逆)な音声符号化なので、完全な
  忠実再現は期待しない。
- **先頭が欠ける/ズレる可能性**: acquisition(信号検出・sync確立)に
  数シンボルかかるため、単発の短い録音では冒頭の音声が復調されずに
  欠落する。実運用の連続送信ではこの遅延は一度きりなので問題にならない。
- **帯域拡張特徴量(21〜35番目)は元音声から流用**: RADEはLPCNetの36要素中
  先頭20要素(スペクトル包絡+ピッチ等)のみを伝送する設計。残り16要素は
  RADEでは送られないため、音声化の際は元音声の値をそのまま流用している
  (本来の運用でもFARGAN側は0埋めや別の補完をする可能性があり、ここは
  簡易的な近似)。

## 次にやるならさらに一歩(今回はスコープ外)
- 本来のRADE運用に近づけるなら、21〜35番目の特徴量をどう扱うべきか
  (ゼロ埋めが正しいか、元音声流用が正しいか)をradae_txe.py/radae_rxe.py
  等の本家スクリプトで確認し、合わせる。
- ノイズを混ぜたチャネルでの復元品質(SNR対品質)の確認。
