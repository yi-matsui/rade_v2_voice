using System;

namespace RadeV2
{
    /// <summary>
    /// TX -> RX ループバックテスト。
    ///
    /// これまでの検証はTX単体・RX単体をそれぞれPython基準と照合していたが、
    /// 「実際にencoderが生成した本物のV2波形を、そのままRXチェーン
    /// (acquisition -> sync -> FrameSyncNet -> decoder)に通して復調できるか」
    /// は未確認だった。合成ノイズ+CP波形ではなく、本物の変調波形での
    /// 初めての一気通貫テストとして実装する。
    ///
    /// 期待する動作: 無雑音チャネル(ループバック)なので、送信したlatentが
    /// そのまま復調され、TX側featuresとRX側features(decoder出力)が
    /// ある程度近い値になる(RADEはこのencoder/decoderペアで学習された
    /// オートエンコーダなので、無雑音なら再構成精度は高いはず)。
    /// 完全な数値一致は期待しない(量子化・acquisitionの遅延・フレーム
    /// アラインメントのズレがあるため)が、sync確立とfeatures取得が
    /// 実際に起きることを確認するのが主目的。
    /// </summary>
    internal static class LoopbackTest
    {
        internal static bool Run()
        {
            Console.WriteLine("\n=== TX->RX ループバックテスト ===");

            using (var rade = new RadeV2Context())
            {
                // 追記部分
                rade.SetBpf(false);       // 例外が出なければ P/Invoke 成功
                rade.SetTimeOffset(0, 0);
                Console.WriteLine("setter OK");

                var rng = new Random(42);
                const int numFrames = 20;

                // --- 1. 複数フレーム分のfeaturesを用意し、TXでIQ化して連結 ---
                // 各フレームのfeatures(84要素)は後で大雑把な照合に使うため保持しておく。
                var sentFeatures = new float[numFrames][];
                var txStream = new System.Collections.Generic.List<RadeComp>();

                // 先頭に無音(低レベルノイズ)を挟み、idle状態からの立ち上がりを見る。
                int silenceSymbols = 8;
                for (int i = 0; i < silenceSymbols * rade.SymLen; i++)
                {
                    txStream.Add(new RadeComp(
                        (float)(rng.NextDouble() - 0.5) * 0.02f,
                        (float)(rng.NextDouble() - 0.5) * 0.02f));
                }

                for (int f = 0; f < numFrames; f++)
                {
                    var features = new float[rade.FeaturesInLen];
                    for (int i = 0; i < features.Length; i++)
                        features[i] = (float)(rng.NextDouble() * 2.0 - 1.0);   // [-1,1] のダミー特徴量
                    sentFeatures[f] = features;

                    var txOut = new RadeComp[rade.TxOutLen];
                    rade.Tx(features, txOut);
                    txStream.AddRange(txOut);
                }

                Console.WriteLine($"TX: {numFrames} フレーム生成、IQストリーム長 = {txStream.Count} サンプル " +
                                  $"(無音 {silenceSymbols * rade.SymLen} + 信号 {numFrames * rade.TxOutLen})");

                // --- 2. RXにストリームを順次流し込む ---
                var stream = txStream.ToArray();
                int prx = 0;
                int nin = rade.SymLen;
                int symbolCount = 0;
                int syncCount = 0;
                int featuresCount = 0;
                bool everSynced = false;
                var recoveredFeaturesList = new System.Collections.Generic.List<float[]>();

                while (prx + nin <= stream.Length)
                {
                    var chunk = new RadeComp[nin];
                    Array.Copy(stream, prx, chunk, 0, nin);
                    int advance = nin;

                    var result = rade.Rx(chunk, ref nin);
                    prx += advance;
                    symbolCount++;

                    if (result.IsSync) { syncCount++; everSynced = true; }
                    if (result.HasFeatures)
                    {
                        featuresCount++;
                        recoveredFeaturesList.Add(result.Features);
                    }
                }

                Console.WriteLine($"RX: {symbolCount} シンボル処理、sync状態={syncCount}回、features取得={featuresCount}回");

                if (!everSynced)
                {
                    Console.WriteLine("!! 一度もsyncしませんでした。acquisition/decoder経路に問題がある可能性。");
                    return false;
                }
                if (featuresCount == 0)
                {
                    Console.WriteLine("!! featuresが一度も得られませんでした。");
                    return false;
                }

                // --- 3. 大雑把な健全性チェック ---
                // 復調されたfeaturesが異常値(NaN/Infや極端な飽和)でないか確認する。
                // 正確なフレーム対応(TX側の何フレーム目に対応するか)はacquisition遅延の
                // ぶんズレるため、ここでは「送信した特徴量群のどれかに近いものが
                // 復調結果群の中にあるか」という緩い基準で健全性を見る。
                int nanOrInf = 0;
                float minVal = float.MaxValue, maxVal = float.MinValue;
                foreach (var feat in recoveredFeaturesList)
                {
                    foreach (var v in feat)
                    {
                        if (float.IsNaN(v) || float.IsInfinity(v)) nanOrInf++;
                        if (v < minVal) minVal = v;
                        if (v > maxVal) maxVal = v;
                    }
                }
                Console.WriteLine($"復調featuresの値域: [{minVal:F3}, {maxVal:F3}]、NaN/Inf数={nanOrInf}");

                if (nanOrInf > 0)
                {
                    Console.WriteLine("!! NaN/Infが検出されました。decoder経路に問題がある可能性。");
                    return false;
                }

                // 最良マッチ(最小二乗誤差)を探し、参考情報として表示する。
                // (量子化・acquisition遅延・フレームアラインメントのズレがあるため、
                //  厳密な一致は期待しないが、桁違いに外れていないかの目安になる)
                float bestMse = float.MaxValue;
                int bestSentIdx = -1, bestRecvIdx = -1;
                for (int si = 0; si < sentFeatures.Length; si++)
                {
                    for (int ri = 0; ri < recoveredFeaturesList.Count; ri++)
                    {
                        float mse = 0;
                        var sent = sentFeatures[si];
                        var recv = recoveredFeaturesList[ri];
                        for (int k = 0; k < sent.Length; k++)
                        {
                            float d = sent[k] - recv[k];
                            mse += d * d;
                        }
                        mse /= sent.Length;
                        if (mse < bestMse) { bestMse = mse; bestSentIdx = si; bestRecvIdx = ri; }
                    }
                }
                Console.WriteLine($"参考: 最良マッチ MSE={bestMse:F4} (送信フレーム{bestSentIdx} <-> 復調{bestRecvIdx}番目)");
                Console.WriteLine("(注: TX/RXのfeaturesは未学習ダミー値のランダム系列のため、" +
                                  "encoder/decoderの再構成精度そのものの評価にはならない。" +
                                  "ここでは経路がクラッシュせず妥当な範囲の値を返すことの確認が主目的)");

                Console.WriteLine("\n=== ループバックテスト 成功(sync確立・features取得を確認) ===");
                return true;
            }
        }
    }
}