using System;

namespace RadeV2
{
    /// <summary>
    /// RADE V2 デコーダ/エンコーダの高水準ラッパー。
    /// rade_v2.dll のネイティブハンドルを IDisposable で確実に解放する。
    ///
    /// 使い方の骨格:
    ///   using var rade = new RadeV2Context();
    ///   int nin = rade.SymLen;
    ///   var rxBuf = new RadeComp[nin];
    ///   // ... rxBuf に IQ サンプルを詰める ...
    ///   var result = rade.Rx(rxBuf, ref nin);
    ///   if (result.HasFeatures) { /* result.Features(84要素)を後段へ */ }
    ///
    ///   var featuresIn = new float[rade.FeaturesInLen];
    ///   var txOut = new RadeComp[rade.TxOutLen];
    ///   rade.Tx(featuresIn, txOut);
    /// </summary>
    public sealed class RadeV2Context : IDisposable
    {
        private IntPtr _handle;
        private bool _disposed;

        public int FeaturesInLen { get; }
        public int TxOutLen { get; }
        public int FeaturesOutLen { get; }
        public int SymLen { get; }
        public int EooOutLen { get; }

        public RadeV2Context()
        {
            _handle = RadeV2Native.rade_v2_open();
            if (_handle == IntPtr.Zero)
                throw new InvalidOperationException("rade_v2_open に失敗しました(重み読み込みエラーの可能性)。");

            FeaturesInLen  = RadeV2Native.rade_v2_n_features_in(_handle);
            TxOutLen       = RadeV2Native.rade_v2_n_tx_out(_handle);
            FeaturesOutLen = RadeV2Native.rade_v2_n_features_out(_handle);
            SymLen         = RadeV2Native.rade_v2_sym_len(_handle);
            EooOutLen      = RadeV2Native.rade_v2_n_eoo_out(_handle);
        }

        /// <summary>1 modem frame 送信。featuresIn は FeaturesInLen 要素、
        /// txOut は TxOutLen 要素(呼び出し側で確保しておくこと)。</summary>
        public void Tx(float[] featuresIn, RadeComp[] txOut)
        {
            ThrowIfDisposed();
            if (featuresIn.Length != FeaturesInLen)
                throw new ArgumentException($"featuresIn は {FeaturesInLen} 要素である必要があります。", nameof(featuresIn));
            if (txOut.Length != TxOutLen)
                throw new ArgumentException($"txOut は {TxOutLen} 要素である必要があります。", nameof(txOut));

            RadeV2Native.rade_v2_tx(_handle, featuresIn, txOut);
        }

        /// <summary>EOO(End Of Over)送信サンプルを取得する。eooOut は EooOutLen 要素。</summary>
        public void TxEoo(RadeComp[] eooOut)
        {
            ThrowIfDisposed();
            if (eooOut.Length != EooOutLen)
                throw new ArgumentException($"eooOut は {EooOutLen} 要素である必要があります。", nameof(eooOut));

            RadeV2Native.rade_v2_tx_eoo(_handle, eooOut);
        }

        /// <summary>
        /// 1 シンボル分の IQ を処理する。
        /// rxIn は呼び出し前の nin 要素以上の長さが必要(呼び出し前に nin
        /// 要素分だけ有効なサンプルが入っていること)。
        /// nin は呼び出し後、次回に必要なサンプル数に更新される
        /// (timing_adj によって値が変わりうるため、呼び出し側は毎回
        /// この値を見てバッファサイズを調整すること)。
        /// </summary>
        public RadeV2RxResult Rx(RadeComp[] rxIn, ref int nin)
        {
            ThrowIfDisposed();
            if (rxIn.Length < nin)
                throw new ArgumentException($"rxIn は少なくとも {nin} 要素必要です。", nameof(rxIn));

            var features = new float[FeaturesOutLen];
            int state = RadeV2Native.rade_v2_rx(_handle, rxIn, ref nin,
                features, out int hasFeatures, out int sigDet, out int sineDet);

            return new RadeV2RxResult
            {
                IsSync = state != 0,
                HasFeatures = hasFeatures != 0,
                Features = hasFeatures != 0 ? features : null,
                SignalDetected = sigDet != 0,
                SineDetected = sineDet != 0,
            };
        }

        /// <summary>受信状態を idle にリセットする(新規復調開始時などに使用)。</summary>
        public void ResetRx()
        {
            ThrowIfDisposed();
            RadeV2Native.rade_v2_rx_reset(_handle);
        }

		// ==== 機能切替 setter ====
        // 本家 rx2.py 既定に準拠(bpf/limit_pitch/timing_adj/agc は既定ON、
        // time_offset は既定 -16,-8、mute は既定OFF)。
        // Python 基準との A/B 照合や、実チャネル/ループバックの条件合わせに使う。

        /// <summary>limit_pitch: feat[18] を -1.4 で下限クリップ(合成ポップ防止)。既定ON。</summary>
        public void SetLimitPitch(bool enable)
        {
            ThrowIfDisposed();
            RadeV2Native.rade_v2_set_limit_pitch(_handle, enable ? 1 : 0);
        }

        /// <summary>mute: 信号喪失時に feat[0]=-5。既定OFF。</summary>
        public void SetMute(bool enable)
        {
            ThrowIfDisposed();
            RadeV2Native.rade_v2_set_mute(_handle, enable ? 1 : 0);
        }

        /// <summary>bpf: 受信入力バンドパスフィルタ。既定ON(実チャネルで帯域外雑音を除去)。</summary>
        public void SetBpf(bool enable)
        {
            ThrowIfDisposed();
            RadeV2Native.rade_v2_set_bpf(_handle, enable ? 1 : 0);
        }

        /// <summary>agc: 自動利得制御。既定ON(実運用向け。本家rx2.py既定はOFF)。</summary>
        public void SetAgc(bool enable)
        {
            ThrowIfDisposed();
            RadeV2Native.rade_v2_set_agc(_handle, enable ? 1 : 0);
        }

        /// <summary>timing_adj: タイミング追従。既定ON。</summary>
        public void SetTimingAdj(bool enable)
        {
            ThrowIfDisposed();
            RadeV2Native.rade_v2_set_timing_adj(_handle, enable ? 1 : 0);
        }

        /// <summary>time_offset / correct_time_offset: DFT窓前倒しと周波数域位相補正。
        /// 既定は本家 rx2.py と同じ -16 / -8。旧動作に戻すなら (0, 0)。</summary>
        public void SetTimeOffset(int timeOffset, int correctTimeOffset)
        {
            ThrowIfDisposed();
            RadeV2Native.rade_v2_set_time_offset(_handle, timeOffset, correctTimeOffset);
        }

        private void ThrowIfDisposed()
        {
            if (_disposed) throw new ObjectDisposedException(nameof(RadeV2Context));
        }

        public void Dispose()
        {
            if (_disposed) return;
            if (_handle != IntPtr.Zero)
            {
                RadeV2Native.rade_v2_close(_handle);
                _handle = IntPtr.Zero;
            }
            _disposed = true;
            GC.SuppressFinalize(this);
        }

        ~RadeV2Context()
        {
            // ファイナライザからはネイティブハンドルの解放のみ行う
            // (マネージドオブジェクトには触れない)。
            if (_handle != IntPtr.Zero)
            {
                RadeV2Native.rade_v2_close(_handle);
                _handle = IntPtr.Zero;
            }
        }
    }

    /// <summary>rade_v2_rx() の呼び出し結果。</summary>
    public sealed class RadeV2RxResult
    {
        /// <summary>受信機が現在 sync 状態か(false なら idle)。</summary>
        public bool IsSync { get; set; }
        /// <summary>このシンボルで有効な features が得られたか
        /// (FrameSyncNet の even/odd 位相選択で勝者フレームだった場合のみ true)。</summary>
        public bool HasFeatures { get; set; }
        /// <summary>HasFeatures が true のときのみ有効。84要素(frames_per_step×feature_dim)。</summary>
        public float[] Features { get; set; }
        public bool SignalDetected { get; set; }
        public bool SineDetected { get; set; }
    }
}
