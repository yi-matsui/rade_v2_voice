/*---------------------------------------------------------------------------*\

  rade_eoo_v2.h

  RADE V2 の EOO(End Of Over)検出。radae_v2.py の _detect_eoo を移植。

  V2 の EOO 検出は「チャネル時間領域スパース性」による(V1 のパイロット
  pend 相関とは全く別方式)。既知の EOO 波形 pend に対する受信信号の
  周波数応答からチャネル h_est を推定し、そのエネルギーが CP 両端に
  集中する度合い(e_cp/e_total)を EOO の指標とする。

  対応 Python (radae_v2.py):
    def _detect_eoo(self):
        pend_fd = fft(model.pend)
        rx_fd   = fft(rx_sym_td)
        active  = |pend_fd| > max(|pend_fd|)*1e-3
        H_est[active] = rx_fd[active]/pend_fd[active]
        h_est   = ifft(H_est)
        e_cp    = Σ|h_est[:Ncp]|² + Σ|h_est[-Ncp:]|²
        eoo_corr = e_cp / e_total
        eoo_smooth = ALPHA_EOO*eoo_smooth + (1-ALPHA_EOO)*eoo_corr
        return eoo_smooth > TEOO

  FFT は kiss_fft を使用(長さ M)。pend_fd は定数なので init で前計算する。

\*---------------------------------------------------------------------------*/

#ifndef RADE_EOO_V2_H
#define RADE_EOO_V2_H

#include "rade_v2_comp.h"
#include "kiss_fft.h"

#define REOO_ALPHA_EOO 0.70f   /* IIR 平滑係数(radae_v2.py ALPHA_EOO) */
#define REOO_TEOO      0.75f   /* EOO 検出しきい値(radae_v2.py TEOO) */
#define REOO_ACTIVE_TH 1e-3f   /* active ビン判定: max*1e-3 */

typedef struct {
    int M;                    /* FFT 長 = OFDM symbol samples */
    int Ncp;                  /* CP 長 */

    kiss_fft_cfg fft_fwd;     /* 順 FFT */
    kiss_fft_cfg fft_inv;     /* 逆 FFT */

    /* 前計算した pend の FFT(定数)。長さ M。 */
    kiss_fft_cpx *pend_fd;    /* [M] */
    float pend_fd_absmax;     /* max(|pend_fd|) */

    /* 作業バッファ(毎回使い回し) */
    kiss_fft_cpx *rx_fd;      /* [M] */
    kiss_fft_cpx *H_est;      /* [M] */
    kiss_fft_cpx *h_est;      /* [M] */
    kiss_fft_cpx *rx_in_c;    /* [M] rx_sym_td を kiss 形式に詰める用 */

    /* 状態 */
    float eoo_smooth;         /* IIR 平滑値 */
    float eoo_corr;           /* 直近の瞬時値 */
} rade_eoo_v2_state;

/* 初期化。pend は M 個の複素定数(model.pend を real/imag 交互 or
   RADE_COMP 配列で渡す)。内部で FFT して pend_fd をキャッシュする。
   確保に失敗したら非0を返す。 */
int  reoo_init(rade_eoo_v2_state *st, int M, int Ncp, const RADE_COMP *pend);

/* 後始末(kiss_fft_cfg とバッファを解放)。 */
void reoo_free(rade_eoo_v2_state *st);

/* EOO 検出を1回実行。rx_sym_td は M 個の複素受信サンプル
   (_extract_symbol が作る周波数補正済み M サンプル)。
   戻り値: EOO 検出なら 1(eoo_smooth > TEOO)、そうでなければ 0。
   eoo_corr / eoo_smooth は状態に保存される。 */
int  reoo_detect(rade_eoo_v2_state *st, const RADE_COMP *rx_sym_td);

#endif /* RADE_EOO_V2_H */
