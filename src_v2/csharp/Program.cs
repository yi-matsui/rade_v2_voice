using System;

namespace RadeV2
{
    /// <summary>
    /// rade_v2.dll の疎通確認用コンソールアプリ。
    /// 1. rade_v2_open/close が例外なく呼べるか
    /// 2. 諸元(FeaturesInLen等)が期待通りか
    /// 3. Tx を1回呼んでクラッシュしないか(ゼロ入力なので出力値の意味は薄いが、
    ///    ネイティブ側でのメモリ破壊が無いことの確認が主目的)
    /// 4. Rx を1回呼んでクラッシュしないか(ノイズ入力)
    /// </summary>
    class Program
    {
        static int Main(string[] mainArgs)
        {
            if (mainArgs.Length >= 2)
            {
                // 引数があれば実音声ループバックテストとして動作する
                // (VoiceLoopbackTest.exe features_raw36.f32 features_hat36.f32)
                return VoiceLoopbackTest.Run(mainArgs);
            }

            Console.WriteLine("=== rade_v2.dll 疎通確認 ===");

            try
            {
                using (var rade = new RadeV2Context())
                {
                    Console.WriteLine("rade_v2_open 成功");
                    Console.WriteLine($"FeaturesInLen  = {rade.FeaturesInLen}  (期待値: 84)");
                    Console.WriteLine($"TxOutLen       = {rade.TxOutLen}       (期待値: 320 = Ns(2)*sym_len(160))");
                    Console.WriteLine($"FeaturesOutLen = {rade.FeaturesOutLen} (期待値: 84)");
                    Console.WriteLine($"SymLen         = {rade.SymLen}         (期待値: 160 = Ncp(32)+M(128))");
                    Console.WriteLine($"EooOutLen      = {rade.EooOutLen}");

                    bool ok = true;
                    if (rade.FeaturesInLen != 84) { Console.WriteLine("!! FeaturesInLen が想定と異なります"); ok = false; }
                    if (rade.SymLen != 160)       { Console.WriteLine("!! SymLen が想定と異なります"); ok = false; }

                    // --- Tx 疎通(ゼロ入力) ---
                    var featuresIn = new float[rade.FeaturesInLen];
                    var txOut = new RadeComp[rade.TxOutLen];
                    rade.Tx(featuresIn, txOut);
                    Console.WriteLine($"Tx 呼び出し成功。txOut[0] = {txOut[0]}, txOut[1] = {txOut[1]}");

                    // --- EOO 疎通 ---
                    var eooOut = new RadeComp[rade.EooOutLen];
                    rade.TxEoo(eooOut);
                    Console.WriteLine($"TxEoo 呼び出し成功。eooOut[0] = {eooOut[0]}");

                    // --- Rx 疎通(ノイズ入力、数シンボル回す) ---
                    var rng = new Random(1234);
                    int nin = rade.SymLen;
                    for (int s = 0; s < 5; s++)
                    {
                        var rxIn = new RadeComp[nin];
                        for (int i = 0; i < nin; i++)
                            rxIn[i] = new RadeComp((float)(rng.NextDouble() - 0.5) * 0.1f,
                                                   (float)(rng.NextDouble() - 0.5) * 0.1f);

                        var result = rade.Rx(rxIn, ref nin);
                        Console.WriteLine($"Rx s={s}: IsSync={result.IsSync} HasFeatures={result.HasFeatures} " +
                                          $"SigDet={result.SignalDetected} SineDet={result.SineDetected} nin(次回)={nin}");
                    }

                    rade.ResetRx();
                    Console.WriteLine("ResetRx 呼び出し成功");

                    Console.WriteLine(ok ? "\n=== 疎通確認 成功 ===" : "\n=== 疎通確認 一部異常 ===");

                    bool loopbackOk = LoopbackTest.Run();

                    bool allOk = ok && loopbackOk;
                    Console.WriteLine(allOk ? "\n=== 全テスト 成功 ===" : "\n=== 一部テスト失敗 ===");
                    return allOk ? 0 : 1;
                }
            }
            catch (Exception ex)
            {
                Console.WriteLine($"例外発生: {ex}");
                return 1;
            }
        }
    }
}
