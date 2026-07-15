/*---------------------------------------------------------------------------*\

  rade_rx_v2.c

  RADE V2 受信機の DSP 部(acquisition + idle 状態機械)。
  radae_v2.py の RADEv2Receiver を、メソッド単位で対応させて移植。

  各関数の冒頭に対応する Python メソッド名を記す。本家 radae_v2.py が
  更新されたら、同名メソッドの差分を対応する rrx_* 関数に反映する。

  [2026-07-09 パッチ] rrx_init の未初期化フィールドを修正。
    hangover / eoo_smooth / eoo_count / state が rrx_init で初期化されて
    いなかった。rade_v2_open() では calloc により偶然ゼロだったため
    露見しなかったが、rade_v2_rx_reset() は rrx_init を呼び直すだけなので、
    **再同期のたびに前セッションの eoo_smooth / eoo_count が残留**していた。
    EOO平滑値が閾値付近に残ったまま再syncすると、直後に誤EOO検出して
    即idleに戻る(=受信が始まらない/途切れる)危険がある。
    hangover も rrx_init では未設定で、呼び出し側(rade_api_v2.c)が
    毎回 75 を代入して補っていた。既定値をここに集約する。

\*---------------------------------------------------------------------------*/

#include <math.h>
#include "rade_rx_v2.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ------------------------------------------------------------------ */
/* __init__                                                            */
/* ------------------------------------------------------------------ */
void rrx_init(rade_rx_v2_state *st,
              int M, int Ncp, int Ns, int Nc, float Fs,
              float w_first, float w_last, int agc_en,
              RADE_COMP *rx_buf, RADE_COMP *rx_phase_vec,
              RADE_COMP *Ry_norm, RADE_COMP *Ry_smooth)
{
    int i;

    st->M       = M;
    st->Ncp     = Ncp;
    st->Ns      = Ns;
    st->Nc      = Nc;
    st->Fs      = Fs;
    st->sym_len = Ncp + M;

    /* target RMS is PAPR (~3 dB) below peak of 1.0 */
    st->agc_en     = agc_en;
    st->agc_target = powf(10.0f, -3.0f / 20.0f);

    /* State machine */
    st->state      = RRX_STATE_IDLE;
    st->count      = 0;
    st->count1     = 0;
    st->n_acq      = 0;
    st->s          = 0;
    st->i          = 0;
    st->timing_adj = 0;

    /* radae_v2.py Args.hangover の既定値。以前は呼び出し側で代入していた */
    st->hangover   = 75;

    /* Tracking estimates */
    st->freq_offset       = 0.0f;
    st->delta_hat         = 0.0f;
    st->delta_hat_g       = 0;
    st->freq_offset_g     = 0.0f;
    st->new_sig_delta_hat = 0;
    st->new_sig_f_hat     = 0;
    st->Ry_max            = 0.0f;
    st->Ry_min            = 0.0f;

    /* Frame-sync discriminators */
    st->frame_sync_even = 0.0f;
    st->frame_sync_odd  = 0.0f;

    /* EOO 状態(reset 時の残留を防ぐため明示的にクリア) */
    st->eoo_smooth = 0.0f;
    st->eoo_count  = 0;

    /* Buffers(呼び出し側が確保。ここでゼロ初期化) */
    st->rx_buf       = rx_buf;
    st->rx_phase_vec = rx_phase_vec;
    st->Ry_norm      = Ry_norm;
    st->Ry_smooth    = Ry_smooth;
    for (i = 0; i < 3 * st->sym_len; i++) st->rx_buf[i]    = rc_make(0.0f, 0.0f);
    for (i = 0; i < st->sym_len;     i++) st->rx_phase_vec[i] = rc_make(0.0f, 0.0f);
    for (i = 0; i < st->sym_len;     i++) st->Ry_norm[i]   = rc_make(0.0f, 0.0f);
    for (i = 0; i < st->sym_len;     i++) st->Ry_smooth[i] = rc_make(0.0f, 0.0f);
    st->rx_phase = rc_make(1.0f, 0.0f);

    /* BPF bandwidth: 1.2*(w[Nc-1]-w[0])*Fs/(2π) */
    st->B_bpf = 1.2f * (w_last - w_first) * st->Fs / (2.0f * (float)M_PI);

    /* SNR estimate 補正 */
    st->snr_offset_dB = 10.0f * log10f(3000.0f / st->B_bpf);
    st->snr_corr_a    = 1.24392558f;
    st->snr_corr_b    = 3.33253932f;
    st->snr_est_dB    = 0.0f;

    /* args 既定 */
    st->fix_delta_hat          = 0;
    st->reset_output_on_resync = 0;
}

/* ------------------------------------------------------------------ */
/* _compute_gain                                                       */
/*   gain = agc_target / (sqrt(mean(|rx_in|^2)) + 1e-6), clip[0.1,10]   */
/* ------------------------------------------------------------------ */
float rrx_compute_gain(const rade_rx_v2_state *st, const RADE_COMP *rx_in, int nin)
{
    int i;
    float sum2, rms, gain;

    if (!st->agc_en) return 1.0f;

    sum2 = 0.0f;
    for (i = 0; i < nin; i++) sum2 += rc_abs2(rx_in[i]);
    rms = sqrtf(sum2 / (float)nin);

    gain = st->agc_target / (rms + 1e-6f);
    if (gain < 0.1f)  gain = 0.1f;
    if (gain > 10.0f) gain = 10.0f;
    return gain;
}

/* ------------------------------------------------------------------ */
/* _update_rx_buf                                                      */
/*   rx_buf[:3L-nin] = rx_buf[nin:];  rx_buf[3L-nin:] = rx_in*gain      */
/* ------------------------------------------------------------------ */
void rrx_update_rx_buf(rade_rx_v2_state *st, const RADE_COMP *rx_in, int nin, float gain)
{
    int i;
    int L3 = 3 * st->sym_len;

    /* 前方シフト: rx_buf[i] = rx_buf[i+nin]  (0 <= i < L3-nin) */
    for (i = 0; i < L3 - nin; i++) st->rx_buf[i] = st->rx_buf[i + nin];

    /* 末尾に新サンプル(gain 適用) */
    for (i = 0; i < nin; i++)
        st->rx_buf[L3 - nin + i] = rc_scale(rx_in[i], gain);
}

/* ------------------------------------------------------------------ */
/* _compute_autocorr                                                   */
/*   for gamma in [0,sym_len):                                         */
/*     idx = sym_len+gamma                                             */
/*     y_cp = rx_buf[idx-Ncp : idx]                                    */
/*     y_m  = rx_buf[idx-Ncp+M : idx+M]                                */
/*     Ry = dot(y_cp, conj(y_m))                                       */
/*     D  = dot(y_cp,conj(y_cp)) + dot(y_m,conj(y_m)) + 1e-12          */
/*     Ry_norm[gamma] = 2*Ry / |D|                                     */
/*   Ry_smooth = ALPHA*Ry_smooth + (1-ALPHA)*Ry_norm                   */
/* ------------------------------------------------------------------ */
void rrx_compute_autocorr(rade_rx_v2_state *st)
{
    int gamma, k;
    int M   = st->M;
    int Ncp = st->Ncp;
    int sym_len = st->sym_len;

    for (gamma = 0; gamma < sym_len; gamma++) {
        int idx = sym_len + gamma;
        int cp_start = idx - Ncp;          /* y_cp = rx_buf[cp_start .. idx)  長さ Ncp */
        int m_start  = idx - Ncp + M;      /* y_m  = rx_buf[m_start  .. idx+M) 長さ Ncp */

        RADE_COMP Ry = rc_make(0.0f, 0.0f);
        float Dcp = 0.0f, Dm = 0.0f;

        for (k = 0; k < Ncp; k++) {
            RADE_COMP a = st->rx_buf[cp_start + k];   /* y_cp[k] */
            RADE_COMP b = st->rx_buf[m_start  + k];   /* y_m[k]  */
            /* Ry += y_cp * conj(y_m) */
            Ry = rc_add(Ry, rc_mul_conj(a, b));
            /* D は自己エネルギー(実数) */
            Dcp += rc_abs2(a);
            Dm  += rc_abs2(b);
        }

        {
            float absD = fabsf(Dcp + Dm + 1e-12f);   /* D は実数なので |D| = D */
            /* Ry_norm[gamma] = 2*Ry / |D| */
            st->Ry_norm[gamma] = rc_scale(Ry, 2.0f / absD);
        }
    }

    /* IIR 平滑: Ry_smooth = ALPHA*Ry_smooth + (1-ALPHA)*Ry_norm */
    for (gamma = 0; gamma < sym_len; gamma++) {
        st->Ry_smooth[gamma] = rc_add(
            rc_scale(st->Ry_smooth[gamma], RRX_ALPHA),
            rc_scale(st->Ry_norm[gamma],   1.0f - RRX_ALPHA));
    }
}

/* ------------------------------------------------------------------ */
/* _detect_signal                                                      */
/*   abs_Ry = |Ry_smooth|                                              */
/*   delta_hat_g = argmax(abs_Ry)   (fix_delta_hat 指定時はそれ)        */
/*   Ry_max = abs_Ry[delta_hat_g];  Ry_min = min(abs_Ry)               */
/*   sig_det  = Ry_max > TSIG                                          */
/*   sine_det = Ry_max/(Ry_min+1e-12) < TSIN                           */
/*   rho = clip(max(abs_Ry), 0, 1-1e-6)                                */
/*   snr_raw = 10*log10(rho/(1-rho)) - snr_offset_dB                   */
/*   snr_est = snr_corr_a*snr_raw + snr_corr_b                         */
/* ------------------------------------------------------------------ */
void rrx_detect_signal(rade_rx_v2_state *st, int *sig_det, int *sine_det)
{
    int gamma;
    int sym_len = st->sym_len;
    int argmax_i = 0, argmin_i = 0;
    float vmax, vmin, rho, snr_raw;

    /* abs_Ry[0] で初期化 */
    float a0 = rc_abs(st->Ry_smooth[0]);
    vmax = a0; vmin = a0;

    for (gamma = 1; gamma < sym_len; gamma++) {
        float a = rc_abs(st->Ry_smooth[gamma]);
        if (a > vmax) { vmax = a; argmax_i = gamma; }
        if (a < vmin) { vmin = a; argmin_i = gamma; }
    }

    if (st->fix_delta_hat) {
        st->delta_hat_g = st->fix_delta_hat;
    } else {
        st->delta_hat_g = argmax_i;
    }

    st->Ry_max = rc_abs(st->Ry_smooth[st->delta_hat_g]);
    st->Ry_min = rc_abs(st->Ry_smooth[argmin_i]);

    *sig_det  = (st->Ry_max > RRX_TSIG) ? 1 : 0;
    *sine_det = (st->Ry_max / (st->Ry_min + 1e-12f) < RRX_TSIN) ? 1 : 0;

    /* SNR 推定(rho は abs_Ry の最大値 = Ry_max ではなく max(abs_Ry)。
       Python は np.max(|Ry_smooth|) を使うので vmax を用いる) */
    rho = vmax;
    if (rho < 0.0f)          rho = 0.0f;
    if (rho > 1.0f - 1e-6f)  rho = 1.0f - 1e-6f;
    snr_raw = 10.0f * log10f(rho / (1.0f - rho) + 1e-12f) - st->snr_offset_dB;
    st->snr_est_dB = st->snr_corr_a * snr_raw + st->snr_corr_b;
}

/* ------------------------------------------------------------------ */
/* _process_idle                                                       */
/*   sig_det && !sine_det で count++、else count=0                      */
/*   count==5 で sync 遷移:                                            */
/*     delta_phi = angle(Ry_smooth[delta_hat_g])                       */
/*     delta_hat = delta_hat_g                                         */
/*     freq_offset = -delta_phi*Fs/(2π*M)                              */
/*     各種カウンタ/平滑リセット、n_acq++                               */
/* ------------------------------------------------------------------ */
int rrx_process_idle(rade_rx_v2_state *st, int sig_det, int sine_det)
{
    if (sig_det && !sine_det) st->count += 1;
    else                      st->count  = 0;

    if (st->count == 5) {
        float delta_phi = rc_arg(st->Ry_smooth[st->delta_hat_g]);
        st->delta_hat   = (float)st->delta_hat_g;
        st->freq_offset = -delta_phi * st->Fs / (2.0f * (float)M_PI * (float)st->M);
        st->count           = 0;
        st->count1          = 0;
        st->frame_sync_even = 0.0f;
        st->frame_sync_odd  = 0.0f;
        st->eoo_smooth      = 0.0f;
        if (st->reset_output_on_resync) st->i = 0;
        st->n_acq += 1;
        return RRX_STATE_SYNC;
    }
    return RRX_STATE_IDLE;
}
