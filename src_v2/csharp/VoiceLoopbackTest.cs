using System;
using System.IO;
using System.Collections.Generic;

namespace RadeV2
{
    /// <summary>
    /// 実音声から抽出した本物の特徴量を使ったTX->RXループバックテスト。
    /// </summary>
    internal static class VoiceLoopbackTest
    {
        private const int NbTotalFeatures = 36;
        private const int NumUsedFeatures = 20;
        private const int FeatureDim = 21;
        private const int FramesPerStep = 4;

        internal static int Run(string[] args)
        {
            if (args.Length < 2)
            {
                Console.WriteLine("usage: VoiceLoopbackTest.exe <features_raw36.f32> <features_hat36.f32>");
                return 1;
            }
            string inPath = args[0];
            string outPath = args[1];

            byte[] rawBytes = File.ReadAllBytes(inPath);
            int totalFloats36 = rawBytes.Length / 4;
            int totalFrames = totalFloats36 / NbTotalFeatures;
            var raw36 = new float[totalFrames * NbTotalFeatures];
            Buffer.BlockCopy(rawBytes, 0, raw36, 0, totalFrames * NbTotalFeatures * 4);

            int numModemFrames = totalFrames / FramesPerStep;
            if (numModemFrames == 0)
            {
                Console.WriteLine("エラー: 入力音声データが短すぎます。");
                return 1;
            }

            using (var rade = new RadeV2Context())
            {
                // 設定をブロック内に配置
                rade.SetBpf(false);
                rade.SetTimeOffset(0, 0);
                Console.WriteLine("setter OK");

                var rng = new Random(7);
                var txStream = new List<RadeComp>();

                // --- 1. TX: 4フレームずつ束ねてIQ化、ストリーム連結 ---
                int preRollSymbols = 12;
                for (int i = 0; i < preRollSymbols * rade.SymLen; i++)
                {
                    txStream.Add(new RadeComp(
                        (float)(rng.NextDouble() - 0.5) * 0.02f,
                        (float)(rng.NextDouble() - 0.5) * 0.02f));
                }

                for (int m = 0; m < numModemFrames; m++)
                {
                    var featuresIn = new float[rade.FeaturesInLen];
                    Array.Copy(raw36, m * FramesPerStep * FeatureDim, featuresIn, 0, featuresIn.Length);
                    var txOut = new RadeComp[rade.TxOutLen];
                    rade.Tx(featuresIn, txOut);
                    txStream.AddRange(txOut);
                }
                Console.WriteLine($"TX: IQストリーム長 = {txStream.Count} サンプル");

                // --- 2. RX: ストリームを順次投入し、復調featuresを収集 ---
                var stream = txStream.ToArray();
                var recoveredFrames = new List<float[]>();
                int prx = 0;
                int nin = rade.SymLen;

                while (prx + nin <= stream.Length)
                {
                    var chunk = new RadeComp[nin];
                    Array.Copy(stream, prx, chunk, 0, nin);
                    int advance = nin;
                    var result = rade.Rx(chunk, ref nin);
                    prx += advance;

                    if (result.HasFeatures)
                    {
                        recoveredFrames.Add((float[])result.Features.Clone());
                    }
                }

                if (recoveredFrames.Count == 0)
                {
                    Console.WriteLine("エラー: featuresが一度も得られませんでした。");
                    return 1;
                }

                // --- 3. 36要素/フレームに復元して書き出す ---
                var outBytes = new byte[recoveredFrames.Count * NbTotalFeatures * 4];
                for (int i = 0; i < recoveredFrames.Count; i++)
                {
                    var frame36 = new float[NbTotalFeatures];
                    Array.Copy(recoveredFrames[i], 0, frame36, 0, NumUsedFeatures);
                    
                    int srcFrame = Math.Min(i, totalFrames - 1);
                    Array.Copy(raw36, srcFrame * NbTotalFeatures + NumUsedFeatures, frame36, NumUsedFeatures, NbTotalFeatures - NumUsedFeatures);
                    
                    Buffer.BlockCopy(frame36, 0, outBytes, i * NbTotalFeatures * 4, NbTotalFeatures * 4);
                }

                File.WriteAllBytes(outPath, outBytes);
                Console.WriteLine($"完了: {outPath} に {recoveredFrames.Count} フレーム書き込みました。");
                
                return 0;
            }
        }
    }
}