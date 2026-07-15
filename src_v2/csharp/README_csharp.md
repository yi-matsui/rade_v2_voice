# rade_v2.dll C# ラッパー 手順

## ファイル構成
- RadeComp.cs       ── RADE_COMP に対応する構造体(blittable, 8バイト)
- RadeV2Native.cs    ── 生のP/Invoke宣言(11関数、internal)
- RadeV2Context.cs   ── IDisposableな高水準ラッパー(公開API、こちらを使う)
- Program.cs         ── 疎通確認用コンソールアプリ
- build_cs_smoketest.bat ── csc.exe でのビルド&実行バッチ

## 手順

### 1. rade_v2.dll をこのフォルダにコピー
```
copy ..\..\src_v2\rade_v2.dll .
```
(rade_v2.lib はC#からは不要。DLLは実行時に動的ロードされる)

### 2. ビルド&実行
```
build_cs_smoketest.bat
```

期待される出力:
```
=== rade_v2.dll 疎通確認 ===
rade_v2_open 成功
FeaturesInLen  = 84  (期待値: 84)
TxOutLen       = 320       (期待値: 320 = Ns(2)*sym_len(160))
FeaturesOutLen = 84 (期待値: 84)
SymLen         = 160         (期待値: 160 = Ncp(32)+M(128))
EooOutLen      = 960
Tx 呼び出し成功。txOut[0] = (...), txOut[1] = (...)
TxEoo 呼び出し成功。eooOut[0] = (...)
Rx s=0: IsSync=False HasFeatures=False SigDet=False SineDet=False nin(次回)=160
Rx s=1: ...
...
ResetRx 呼び出し成功

=== 疎通確認 成功 ===
```
(ノイズ入力なので sync に入ることはない想定。ここではクラッシュせず
一連のAPIが正しく呼べることの確認が主目的)

## 上位コードでの使い方(骨格)

### 受信ループ
```csharp
using var rade = new RadeV2Context();
int nin = rade.SymLen;
var rxBuf = new RadeComp[rade.SymLen * 2]; // timing_adjで多少伸びる可能性を見込み余裕を持つ

while (/* IQストリームが続く間 */)
{
    // rxBuf の先頭 nin 要素に IQ サンプルを詰める(外部からの取得はアプリ次第)
    FillIqSamples(rxBuf, nin);

    var result = rade.Rx(rxBuf, ref nin);
    if (result.HasFeatures)
    {
        // result.Features(84要素 = frames_per_step(4) x feature_dim(21))
        // を FARGAN 等の後段デコーダへ渡す
        SendToVocoder(result.Features);
    }
    if (!result.IsSync)
    {
        // idle に戻った(信号ロストやEOO検出)。UIに状態表示するなど。
    }
}
```

### 送信
```csharp
using var rade = new RadeV2Context();
var featuresIn = new float[rade.FeaturesInLen]; // 84要素、エンコーダ入力
var txOut = new RadeComp[rade.TxOutLen];        // 320要素、送信IQ(CP込み)

// featuresIn に音声特徴量(4フレーム分, 21次元)を詰める
rade.Tx(featuresIn, txOut);
// txOut を D/A やサウンドデバイスへ送信(実部/虚部からSSB変調等、後段の仕事)
```

### EOO送信
```csharp
var eooOut = new RadeComp[rade.EooOutLen];
rade.TxEoo(eooOut);
// 送信終了時にこのIQサンプルを1回送る
```

## 注意点
- **スレッド安全性は未検証**: RadeV2Context 1インスタンスを複数スレッドから
  同時に呼ぶことは想定していない。受信・送信を別スレッドで行う場合は、
  それぞれ別の RadeV2Context インスタンスを使うこと(rade_v2_open を
  2回呼んで2つのコンテキストを持つ設計が安全)。
- **DLL探索パス**: rade_v2.dll は実行ファイル(.exe)と同じフォルダに
  置くのが最も確実。それ以外の場所に置く場合はPATH環境変数か
  SetDllDirectory 等で解決する必要がある。
- **RadeCallTest への統合時**: 既存のSV1EIAアレンジV2コードは破棄し、
  このRadeV2Context経由で自前rade_v2.dllを呼ぶ形に置き換える。
  V1のRadeNative.cs/RadeComp.csと構造は似ているが、V2はPython埋め込み
  ではないため RadeEnvironment.Bootstrap(conda/PYTHONHOME設定等)は不要
  になり、C#側の初期化コードを大幅に簡素化できる。

## 未実装/次回の課題
- Rx呼び出し時のrxBufサイズ管理: nin は timing_adj により呼び出しごとに
  変わりうる(±sym_len/4程度)。上記の骨格では余裕を持ったバッファを
  使う想定だが、実際の運用ではリングバッファ等でストリームから
  必要量だけ取り出す設計にするのが望ましい。
- 実際のIQデータ(KiwiSDR等)を使った実地動作確認は未実施。
