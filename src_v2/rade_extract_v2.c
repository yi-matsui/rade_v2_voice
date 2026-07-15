/*---------------------------------------------------------------------------*\

  rade_extract_v2.c

  RADE V2 _extract_symbol + receiver(run_decoder=False)の移植。
  周波数補正 → CP除去 → DFT → QPSK demap → latent z_hat。

  [2026-07-09 パッチ] time_offset / correct_time_offset を実装。
  旧版は「V2: time_offset=0, correct_time_offset=False」と誤認していたが、
  本家 rx2.py の既定は time_offset=-16, correct_time_offset=-8 である
  (rx2.py の argparse 既定値。radae.py receiver 474/486-495行目で適用)。
  これは V1→V2 移植で繰り返した「値の継承ミス」パターンの第5号。
  クリス氏実装も RADC_V2_TIME_OFFSET=-16 / RADC_V2_CORRECT_TIME_OFFSET=-8
  として実装済み(radc_const_v2.h)。

  本家の適用式(radae/radae.py, 一次ソースで確認済み):
    rx_dash = rx[:,:, Ncp+time_offset : Ncp+time_offset+M]     # DFT窓を前倒し
    if correct_time_offset:                                     # 0なら不適用
        rx_sym *= exp(-1j * correct_time_offset * w[c])         # キャリア毎位相

  w[c] は 2π*(carrier_1_index+c)/M の整数DFTビン。C側では Wfwd から
  w[c] = -arg(Wfwd[1*Nc+c]) で自己完結的に導出する(Wfwd[n,c]=exp(-j n w_c)
  のため。checkpoint を変えても再export不要で常に整合する)。

  ※ 旧基準(.f32)との回帰照合時は rext_set_time_offset(st, 0, 0) で
    旧動作に戻せる(既定は本家仕様の -16 / -8)。

\*---------------------------------------------------------------------------*/

#include <math.h>
#include "rade_extract_v2.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* correct_time_offset のキャリア毎位相 exp(-j*ct*w[c]) を事前計算 */
static void update_ct_phase(rade_extract_v2_state *st)
{
    int c;
    for (c = 0; c < st->Nc; c++) {
        double ang = -(double)st->correct_time_offset * (double)st->w[c];
        st->ct_phase[c] = rc_make((float)cos(ang), (float)sin(ang));
    }
}

void rext_set_time_offset(rade_extract_v2_state *st,
                          int time_offset, int correct_time_offset)
{
    /* 前提: Ncp + time_offset >= 0(DFT窓がシンボル先頭より前に出ない)。
       V2 は Ncp=32, time_offset=-16 なので満たす。 */
    st->time_offset         = time_offset;
    st->correct_time_offset = correct_time_offset;
    update_ct_phase(st);
}

void rext_init(rade_extract_v2_state *st,
               int M, int Ncp, int Ns, int Nc, int latent_dim, float Fs,
               const RADE_COMP *Wfwd,
               RADE_COMP *rx_phase_vec, RADE_COMP *rx_i, RADE_COMP *rx_sym_td)
{
    int i, c;
    st->M = M; st->Ncp = Ncp; st->Ns = Ns; st->Nc = Nc;
    st->latent_dim = latent_dim; st->Fs = Fs;
    st->sym_len = Ncp + M;
    st->Wfwd = Wfwd;

    st->rx_phase = rc_make(1.0f, 0.0f);   /* Python: rx_phase = 1 + 0j */

    st->rx_phase_vec = rx_phase_vec;
    st->rx_i         = rx_i;
    st->rx_sym_td    = rx_sym_td;

    for (i = 0; i < st->sym_len;        i++) st->rx_phase_vec[i] = rc_make(0.0f,0.0f);
    for (i = 0; i < st->Ns*st->sym_len; i++) st->rx_i[i]         = rc_make(0.0f,0.0f);
    for (i = 0; i < st->M;              i++) st->rx_sym_td[i]    = rc_make(0.0f,0.0f);

    /* w[c] を Wfwd から導出: Wfwd[n*Nc+c] = exp(-j*n*w_c) なので
       w_c = -arg(Wfwd[1*Nc + c])。w_c は (0, π) の範囲なので位相折返しなし。 */
    for (c = 0; c < Nc; c++)
        st->w[c] = -rc_arg(st->Wfwd[Nc + c]);

    /* 本家 rx2.py の既定値(--time_offset -16 / --correct_time_offset -8) */
    st->time_offset         = -16;
    st->correct_time_offset = -8;
    update_ct_phase(st);
}

void rext_extract(rade_extract_v2_state *st,
                  const RADE_COMP *rx_buf,
                  float delta_hat, float freq_offset,
                  float *z_hat_out)
{
    int M = st->M, Ncp = st->Ncp, Ns = st->Ns, Nc = st->Nc;
    int sym_len = st->sym_len;
    int latent_dim = st->latent_dim;
    int n, c, sidx, k;

    /* --- 位相ベクトル生成(Python: omega, rx_phase を毎サンプル回す) --- */
    /* omega = 2π*freq_offset/Fs.  rx_phase *= exp(-jω) を sym_len 回 */
    float omega = 2.0f * (float)M_PI * freq_offset / st->Fs;
    RADE_COMP rot = rc_expj(-omega);   /* exp(-jω) */
    for (n = 0; n < sym_len; n++) {
        st->rx_phase = rc_mul(st->rx_phase, rot);
        st->rx_phase_vec[n] = st->rx_phase;
    }

    /* --- 切り出し: st = sym_len + (delta_hat - Ncp), en = st + sym_len --- */
    int delta_hat_rx = (int)(delta_hat - (float)Ncp);
    int start = sym_len + delta_hat_rx;   /* rx_buf 上の開始位置 */

    /* rx_i を1シンボル分スライド: 前半 <= 後半 */
    for (n = 0; n < sym_len; n++)
        st->rx_i[n] = st->rx_i[sym_len + n];
    /* 後半 = rx_phase_vec * rx_buf[start .. start+sym_len) */
    for (n = 0; n < sym_len; n++)
        st->rx_i[sym_len + n] = rc_mul(st->rx_phase_vec[n], rx_buf[start + n]);

    /* rx_sym_td = 周波数補正後シンボルの CP 除去部([Ncp:] = M サンプル)
       注意: EOO検出用のこのバッファは time_offset を適用しない
       (Python _extract_symbol: rx_sym_td = (...)[Ncp:] そのまま)。 */
    for (n = 0; n < M; n++)
        st->rx_sym_td[n] = st->rx_i[sym_len + Ncp + n];

    /* --- receiver(rx_i): CP除去(time_offset分前倒し) → DFT → 位相補正 → demap --- */
    /* 各シンボル s の DFT窓は [s*sym_len + Ncp + time_offset, +M)。
       Python: rx_dash = rx[:,:, Ncp+time_offset : Ncp+time_offset+M] */
    k = 0;  /* 複素シンボルの通し番号(0 .. Ns*Nc-1) */
    for (sidx = 0; sidx < Ns; sidx++) {
        int win_base = sidx * sym_len + Ncp + st->time_offset;   /* DFT窓先頭 */
        for (c = 0; c < Nc; c++) {
            /* rx_sym[sidx,c] = Σ_m rx_i[win_base+m] * Wfwd[m*Nc + c] */
            RADE_COMP acc = rc_make(0.0f, 0.0f);
            for (n = 0; n < M; n++) {
                acc = rc_add(acc, rc_mul(st->rx_i[win_base + n], st->Wfwd[n*Nc + c]));
            }
            /* correct_time_offset: rx_sym *= exp(-j*ct*w[c])(ct=0なら恒等) */
            if (st->correct_time_offset != 0)
                acc = rc_mul(acc, st->ct_phase[c]);
            /* z_hat[2k]=real, z_hat[2k+1]=imag */
            z_hat_out[2*k]     = acc.real;
            z_hat_out[2*k + 1] = acc.imag;
            k++;
        }
    }
    /* k == Ns*Nc == latent_dim/2 のはず。z_hat_out は latent_dim 要素。 */
    (void)latent_dim;
}
