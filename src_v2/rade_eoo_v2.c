/*---------------------------------------------------------------------------*\

  rade_eoo_v2.c

  RADE V2 EOO 検出(channel time-domain sparsity)。
  radae_v2.py の _detect_eoo を移植。kiss_fft(標準 Borgerding 版)を使用。

\*---------------------------------------------------------------------------*/

#include <stdlib.h>
#include <math.h>
#include "rade_eoo_v2.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* RADE_COMP -> kiss_fft_cpx 変換(メンバ名 real/imag -> r/i) */
static inline kiss_fft_cpx rc2k(RADE_COMP z) {
    kiss_fft_cpx k; k.r = z.real; k.i = z.imag; return k;
}

int reoo_init(rade_eoo_v2_state *st, int M, int Ncp, const RADE_COMP *pend)
{
    int i;

    st->M   = M;
    st->Ncp = Ncp;
    st->eoo_smooth = 0.0f;
    st->eoo_corr   = 0.0f;

    /* kiss_fft cfg: 順(inverse=0)と逆(inverse=1) */
    st->fft_fwd = kiss_fft_alloc(M, 0, NULL, NULL);
    st->fft_inv = kiss_fft_alloc(M, 1, NULL, NULL);
    if (!st->fft_fwd || !st->fft_inv) return 1;

    st->pend_fd = (kiss_fft_cpx*)malloc(sizeof(kiss_fft_cpx) * M);
    st->rx_fd   = (kiss_fft_cpx*)malloc(sizeof(kiss_fft_cpx) * M);
    st->H_est   = (kiss_fft_cpx*)malloc(sizeof(kiss_fft_cpx) * M);
    st->h_est   = (kiss_fft_cpx*)malloc(sizeof(kiss_fft_cpx) * M);
    st->rx_in_c = (kiss_fft_cpx*)malloc(sizeof(kiss_fft_cpx) * M);
    if (!st->pend_fd || !st->rx_fd || !st->H_est || !st->h_est || !st->rx_in_c)
        return 2;

    /* pend を kiss 形式に詰めて FFT → pend_fd(定数キャッシュ) */
    {
        kiss_fft_cpx *pend_c = (kiss_fft_cpx*)malloc(sizeof(kiss_fft_cpx) * M);
        if (!pend_c) return 3;
        for (i = 0; i < M; i++) pend_c[i] = rc2k(pend[i]);
        kiss_fft(st->fft_fwd, pend_c, st->pend_fd);
        free(pend_c);
    }

    /* max(|pend_fd|) を前計算 */
    st->pend_fd_absmax = 0.0f;
    for (i = 0; i < M; i++) {
        float a = sqrtf(st->pend_fd[i].r * st->pend_fd[i].r +
                        st->pend_fd[i].i * st->pend_fd[i].i);
        if (a > st->pend_fd_absmax) st->pend_fd_absmax = a;
    }

    return 0;
}

void reoo_free(rade_eoo_v2_state *st)
{
    /* kiss_fft_alloc は malloc なので free で解放(kiss_fft_free = free) */
    if (st->fft_fwd) { free(st->fft_fwd); st->fft_fwd = NULL; }
    if (st->fft_inv) { free(st->fft_inv); st->fft_inv = NULL; }
    if (st->pend_fd) { free(st->pend_fd); st->pend_fd = NULL; }
    if (st->rx_fd)   { free(st->rx_fd);   st->rx_fd   = NULL; }
    if (st->H_est)   { free(st->H_est);   st->H_est   = NULL; }
    if (st->h_est)   { free(st->h_est);   st->h_est   = NULL; }
    if (st->rx_in_c) { free(st->rx_in_c); st->rx_in_c = NULL; }
}

int reoo_detect(rade_eoo_v2_state *st, const RADE_COMP *rx_sym_td)
{
    int i;
    int M   = st->M;
    int Ncp = st->Ncp;
    float threshold = st->pend_fd_absmax * REOO_ACTIVE_TH;

    /* rx_fd = fft(rx_sym_td) */
    for (i = 0; i < M; i++) st->rx_in_c[i] = rc2k(rx_sym_td[i]);
    kiss_fft(st->fft_fwd, st->rx_in_c, st->rx_fd);

    /* H_est[active] = rx_fd/pend_fd, それ以外は 0
       active = |pend_fd| > max*1e-3 */
    for (i = 0; i < M; i++) {
        float pr = st->pend_fd[i].r;
        float pi = st->pend_fd[i].i;
        float pabs = sqrtf(pr * pr + pi * pi);
        if (pabs > threshold) {
            /* 複素除算: (rx_fd) / (pend_fd) = rx_fd * conj(pend_fd) / |pend_fd|^2 */
            float rr = st->rx_fd[i].r;
            float ri = st->rx_fd[i].i;
            float denom = pr * pr + pi * pi;       /* = pabs^2 */
            st->H_est[i].r = (rr * pr + ri * pi) / denom;
            st->H_est[i].i = (ri * pr - rr * pi) / denom;
        } else {
            st->H_est[i].r = 0.0f;
            st->H_est[i].i = 0.0f;
        }
    }

    /* h_est = ifft(H_est)
       注意: kiss_fft の逆変換は正規化されない(1/N が掛からない)。
       numpy.ifft は 1/N 正規化する。ここでは e_cp/e_total の「比」を
       取るため、全要素に共通の 1/N は約分されて結果に影響しない。
       よって正規化は省略してよい(比が一致する)。 */
    kiss_fft(st->fft_inv, st->H_est, st->h_est);

    /* e_total = Σ|h_est|^2, e_cp = Σ|h_est[:Ncp]|^2 + Σ|h_est[-Ncp:]|^2 */
    {
        float e_total = 1e-12f;
        float e_cp = 0.0f;
        for (i = 0; i < M; i++) {
            float hr = st->h_est[i].r;
            float hi = st->h_est[i].i;
            e_total += hr * hr + hi * hi;
        }
        for (i = 0; i < Ncp; i++) {
            /* 先頭 Ncp 個 */
            float hr0 = st->h_est[i].r;
            float hi0 = st->h_est[i].i;
            e_cp += hr0 * hr0 + hi0 * hi0;
            /* 末尾 Ncp 個: h_est[M-Ncp .. M-1] */
            float hr1 = st->h_est[M - Ncp + i].r;
            float hi1 = st->h_est[M - Ncp + i].i;
            e_cp += hr1 * hr1 + hi1 * hi1;
        }

        st->eoo_corr = e_cp / e_total;
    }

    /* IIR 平滑 */
    st->eoo_smooth = REOO_ALPHA_EOO * st->eoo_smooth
                   + (1.0f - REOO_ALPHA_EOO) * st->eoo_corr;

    return (st->eoo_smooth > REOO_TEOO) ? 1 : 0;
}
