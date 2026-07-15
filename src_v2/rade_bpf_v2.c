/*---------------------------------------------------------------------------*\

  rade_bpf_v2.c

  受信入力用 複素バンドパスフィルタ。radae/dsp.py class complex_bpf の移植。
  対応関係:
    rbpf_init()    <-> complex_bpf.__init__
    rbpf_process() <-> complex_bpf.bpf
  本家 dsp.py が更新されたら同名メソッドの差分をここに反映する。

\*---------------------------------------------------------------------------*/

#include <math.h>
#include <string.h>
#include "rade_bpf_v2.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* np.sinc(x) = sin(pi*x)/(pi*x)(正規化sinc)。係数生成はdoubleで行う */
static double sinc_norm(double x)
{
    double px;
    if (x == 0.0) return 1.0;
    px = M_PI * x;
    return sin(px) / px;
}

void rbpf_init(rade_bpf_v2_state *st, int ntap, float fs_hz,
               float bandwidth_hz, float centre_hz)
{
    int i;
    double B = (double)bandwidth_hz / (double)fs_hz;

    st->ntap  = ntap;
    st->alpha = 2.0f * (float)M_PI * centre_hz / fs_hz;

    /* 実係数ローパス: h[i] = B * sinc(n*B), n = i-(Ntap-1)/2(対称) */
    for (i = 0; i < ntap; i++) {
        double n = (double)i - (double)(ntap - 1) / 2.0;
        st->h[i] = (float)(B * sinc_norm(n * B));
    }

    rbpf_reset(st);
}

void rbpf_reset(rade_bpf_v2_state *st)
{
    memset(st->mem, 0, sizeof(st->mem));
    memset(st->win, 0, sizeof(st->win));
    st->phase = rc_make(1.0f, 0.0f);
}

void rbpf_process(rade_bpf_v2_state *st, RADE_COMP *out,
                  const RADE_COMP *in, int n)
{
    int hist = st->ntap - 1;
    int k, t;
    RADE_COMP pv[RADE_BPF_V2_MAXLEN];
    RADE_COMP phase0 = st->phase;

    if (n <= 0) return;

    /* --- phase_vec = phase * exp(-1j*alpha*(k+1)) ---
       本家同様、ブロック先頭位相 x 直接計算テーブルの積。
       角度は double で計算し、サンプル毎の累積乗算による
       振幅/位相ドリフトを避ける */
    for (k = 0; k < n; k++) {
        double ang = -(double)st->alpha * (double)(k + 1);
        RADE_COMP e = rc_make((float)cos(ang), (float)sin(ang));
        pv[k] = rc_mul(phase0, e);
    }

    /* --- 履歴 + ベースバンド(ミックスダウン)を win に連結 ---
       x_mem = concat(mem, x*phase_vec) に対応 */
    memcpy(st->win, st->mem, sizeof(RADE_COMP) * (size_t)hist);
    for (k = 0; k < n; k++)
        st->win[hist + k] = rc_mul(in[k], pv[k]);

    /* --- 状態保存(out==in のin-place対応のため、出力書き込みより先に、
       次ブロック用の末尾 Ntap-1 サンプルと位相を確定させる) --- */
    memcpy(st->mem, &st->win[n], sizeof(RADE_COMP) * (size_t)hist);
    st->phase = pv[n - 1];

    /* --- 畳み込み + ミックスアップ ---
       x_filt[k] = dot(x_mem[k:k+Ntap], h);  out = x_filt * conj(phase_vec)
       h は対称なので時間反転省略(本家assertと同じ前提) */
    for (k = 0; k < n; k++) {
        RADE_COMP acc = rc_make(0.0f, 0.0f);
        const RADE_COMP *w = &st->win[k];
        for (t = 0; t < st->ntap; t++)
            acc = rc_add(acc, rc_scale(w[t], st->h[t]));
        out[k] = rc_mul_conj(acc, pv[k]);   /* acc * conj(pv[k]) */
    }
}
