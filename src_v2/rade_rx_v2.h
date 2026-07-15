/*---------------------------------------------------------------------------*\

  rade_rx_v2.h

  RADE V2 受信機(acquisition + frame sync 状態機械)の C 実装。
  本家 radae_v2.py の RADEv2Receiver を移植したもの。

  この段階(DSP 部)で移植する範囲:
    __init__          -> rrx_init
    _compute_gain     -> rrx_compute_gain
    _update_rx_buf    -> rrx_update_rx_buf
    _compute_autocorr -> rrx_compute_autocorr
    _detect_signal    -> rrx_detect_signal
    _process_idle     -> rrx_process_idle

  未実装(後続の段):
    _extract_symbol / _detect_eoo / _update_frame_sync_and_decode /
    _process_sync / _adjust_timing / _process_symbol

  複素数は RADE_COMP(float2)で扱う。Python の numpy complex64 と一致。

\*---------------------------------------------------------------------------*/

#ifndef RADE_RX_V2_H
#define RADE_RX_V2_H

#include "rade_v2_comp.h"     /* RADE_COMP と複素演算 */

/* 状態機械の状態 */
#define RRX_STATE_IDLE 0
#define RRX_STATE_SYNC 1

/* IIR / 検出しきい値(radae_v2.py のクラス定数と同名・同値) */
#define RRX_ALPHA      0.95f   /* Ry_smooth IIR */
#define RRX_BETA       0.999f  /* delta_hat / freq_offset IIR (+ frame_sync gamma) */
#define RRX_TSIG       0.38f   /* 信号検出しきい値 |Ry_smooth| */
#define RRX_TSIN       4.0f    /* 正弦波検出比しきい値 */
#define RRX_TEOO       0.75f   /* EOO 検出しきい値(平滑後) */
#define RRX_ALPHA_EOO  0.70f   /* EOO 平滑 IIR */

typedef struct {
    /* geometry (model 由来) */
    int   M;          /* OFDM symbol samples (V2: 128) */
    int   Ncp;        /* cyclic prefix (32) */
    int   Ns;         /* data symbols per modem frame (V2: 2) */
    int   Nc;         /* carriers (V2: 14) */
    int   sym_len;    /* Ncp + M */
    float Fs;         /* sample rate (8000) */

    /* AGC */
    int   agc_en;     /* args.agc 相当 */
    float agc_target; /* 10^(-3/20) */

    /* 状態機械 */
    int   state;      /* RRX_STATE_* */
    int   count, count1, n_acq, s, i, timing_adj;

    /* 追従推定 */
    float freq_offset, freq_offset_g;
    float delta_hat;      /* float(IIR で更新, _process_sync/idle が使う) */
    int   delta_hat_g;    /* argmax インデックス */
    int   new_sig_delta_hat, new_sig_f_hat;
    float Ry_max, Ry_min;

    /* 統合部(_process_sync)用 */
    int   hangover;       /* args.hangover(signal loss 判定) */
    int   eoo_count;      /* EOO 連続ヒット */

    /* フレーム同期(odd/even) */
    float frame_sync_even, frame_sync_odd;

    /* EOO 平滑(idle リセットで参照。EOO 検出本体は後続の段) */
    float eoo_smooth;

    /* バッファ(いずれも呼び出し側で確保、rrx_init が受け取る) */
    RADE_COMP *rx_buf;        /* 3*sym_len */
    RADE_COMP *rx_phase_vec;  /* sym_len */
    RADE_COMP *Ry_norm;       /* sym_len */
    RADE_COMP *Ry_smooth;     /* sym_len */
    RADE_COMP  rx_phase;      /* 実行中の位相アキュムレータ */

    /* SNR 推定 */
    float B_bpf;
    float snr_offset_dB;
    float snr_corr_a, snr_corr_b;
    float snr_est_dB;

    /* args 相当(idle/sync で参照) */
    int   fix_delta_hat;              /* 0 = 自動(argmax) */
    int   reset_output_on_resync;
} rade_rx_v2_state;

/* 初期化。
   M,Ncp,Ns,Nc,Fs は model 由来。w_first/w_last は角周波数 w[0], w[Nc-1]
   (B_bpf/snr_offset 算出用)。agc_en は AGC 有効フラグ。
   バッファ群は呼び出し側で確保して渡す(長さは上記コメント参照)。 */
void rrx_init(rade_rx_v2_state *st,
              int M, int Ncp, int Ns, int Nc, float Fs,
              float w_first, float w_last, int agc_en,
              RADE_COMP *rx_buf, RADE_COMP *rx_phase_vec,
              RADE_COMP *Ry_norm, RADE_COMP *Ry_smooth);

/* AGC ゲイン算出(rx_in[nin] の RMS から)。agc 無効なら 1.0。 */
float rrx_compute_gain(const rade_rx_v2_state *st, const RADE_COMP *rx_in, int nin);

/* リングバッファ更新: 末尾に rx_in*gain を詰める。 */
void rrx_update_rx_buf(rade_rx_v2_state *st, const RADE_COMP *rx_in, int nin, float gain);

/* CP 自己相関 Ry_norm を計算し Ry_smooth を IIR 更新。 */
void rrx_compute_autocorr(rade_rx_v2_state *st);

/* 信号検出。sig_det/sine_det を出力し、SNR も更新。 */
void rrx_detect_signal(rade_rx_v2_state *st, int *sig_det, int *sine_det);

/* idle 状態処理。戻り値: 次状態(RRX_STATE_*)。 */
int rrx_process_idle(rade_rx_v2_state *st, int sig_det, int sine_det);

#endif /* RADE_RX_V2_H */
