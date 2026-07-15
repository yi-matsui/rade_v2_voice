using System;
using System.Runtime.InteropServices;

namespace RadeV2
{
    /// <summary>
    /// rade_v2.dll の生の P/Invoke 宣言。rade_api_v2.h の11関数に
    /// 一対一対応する。呼び出し規約は cdecl(MSVCのデフォルト)。
    ///
    /// 上位コードはこのクラスを直接使わず、RadeV2Context(ラッパークラス)
    /// を介して使うことを推奨(IDisposable でハンドル解放を保証できるため)。
    /// </summary>
    internal static class RadeV2Native
    {
        private const string DllName = "rade_v2.dll";

        // ---- 生成・破棄 ----

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr rade_v2_open();

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void rade_v2_close(IntPtr ctx);

        // ---- 諸元取得 ----

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int rade_v2_n_features_in(IntPtr ctx);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int rade_v2_n_tx_out(IntPtr ctx);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int rade_v2_n_features_out(IntPtr ctx);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int rade_v2_sym_len(IntPtr ctx);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int rade_v2_n_eoo_out(IntPtr ctx);

        // ---- 送信 ----
        // features_in / tx_out は呼び出し側で確保した配列をそのまま渡す。
        // RadeComp[] は blittable な構造体配列なので、そのままマーシャリング
        // できる(コピー不要、ピン留めのみ)。

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void rade_v2_tx(IntPtr ctx,
            [In] float[] featuresIn,
            [Out] RadeComp[] txOut);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void rade_v2_tx_eoo(IntPtr ctx,
            [Out] RadeComp[] eooOut);

        // ---- 受信 ----
        // nin_io は C 側で読み書きされる(呼び出し前:今回処理するサンプル数,
        // 呼び出し後:次回に必要なサンプル数)ため ref int で渡す。

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int rade_v2_rx(IntPtr ctx,
            [In] RadeComp[] rxIn,
            ref int ninIo,
            [Out] float[] featuresOut,
            out int hasFeaturesOut,
            out int sigDetOut,
            out int sineDetOut);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void rade_v2_rx_reset(IntPtr ctx);

		// ---- 機能切替 setter(A/B検証・条件合わせ用。既定は本家 rx2.py 準拠)----

        // limit_pitch: feat[18] を -1.4 でクリップ(既定ON、合成ポップ防止)
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void rade_v2_set_limit_pitch(IntPtr ctx, int enable);

        // mute: 信号喪失時 feat[0]=-5(既定OFF)
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void rade_v2_set_mute(IntPtr ctx, int enable);

        // bpf: 受信入力BPF(既定ON、実チャネルで帯域外雑音を除去)
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void rade_v2_set_bpf(IntPtr ctx, int enable);

        // agc: 自動利得制御(既定ON、実運用向け。rx2.py既定はOFF)
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void rade_v2_set_agc(IntPtr ctx, int enable);

        // timing_adj: タイミング追従(既定ON)
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void rade_v2_set_timing_adj(IntPtr ctx, int enable);

        // time_offset: DFT窓前倒し/位相補正(既定 -16,-8。本家rx2.py既定)
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void rade_v2_set_time_offset(IntPtr ctx,
            int timeOffset, int correctTimeOffset);
    }
}
