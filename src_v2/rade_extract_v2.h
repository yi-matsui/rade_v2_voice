/*---------------------------------------------------------------------------*\

  rade_extract_v2.h

  RADE V2 受信の _extract_symbol + model.receiver(run_decoder=False)を移植。
  周波数補正 → シンボル切り出し → OFDM 復調(CP除去+DFT)→ QPSK demap →
  latent z_hat を得る。

  V2 の receiver は pilots=False, time_offset=0, correct_time_offset=False
  なので、以下の最小手順に単純化される(radae.py receiver の V2 経路):
    1. CP 除去: 各シンボルの [Ncp : Ncp+M] を取る
    2. DFT   : rx_sym = rx_dash @ Wfwd  (M時間 → Nc周波数)
    3. QPSK demap: rx_sym を latent_dim/2 ごとに、real→偶index, imag→奇index

  対応 Python (radae_v2.py):
    def _extract_symbol(self):
        omega=2π*freq_offset/Fs
        rx_phase を毎サンプル回して rx_phase_vec[n] に記録
        st=sym_len+(delta_hat-Ncp); en=st+sym_len
        rx_i 前半=後半; rx_i 後半 = rx_phase_vec * rx_buf[st:en]
        rx_sym_td = (rx_phase_vec * rx_buf[st:en])[Ncp:]
        return receiver(rx_i, run_decoder=False)

  Wfwd は定数(M x Nc の複素 DFT 行列)。init で受け取りキャッシュする。

\*---------------------------------------------------------------------------*/

#ifndef RADE_EXTRACT_V2_H
#define RADE_EXTRACT_V2_H

#include "rade_v2_comp.h"

typedef struct {
    int   M;          /* 128 */
    int   Ncp;        /* 32 */
    int   Ns;         /* 2 */
    int   Nc;         /* 14 */
    int   latent_dim; /* 56 */
    int   sym_len;    /* Ncp + M = 160 */
    float Fs;         /* 8000 */

    /* DFT 行列 Wfwd: (M x Nc) 複素。rx_dash[M] @ Wfwd = rx_sym[Nc]。
       行優先 [m*Nc + c] で格納。 */
    const RADE_COMP *Wfwd;  /* [M*Nc] 呼び出し側が保持(定数) */

	/* 本家 rx2.py 既定: -16 / -8(radae.py receiver 474/486行) */
    int       time_offset;
    int       correct_time_offset;
    float     w[32];          /* キャリア角周波数(Wfwdから導出, Nc<=32) */
    RADE_COMP ct_phase[32];   /* exp(-j*ct*w[c]) 事前計算 */

    /* 位相アキュムレータ(呼び出し間で持続) */
    RADE_COMP rx_phase;

    /* 作業バッファ(呼び出し側が確保) */
    RADE_COMP *rx_phase_vec;  /* [sym_len] */
    RADE_COMP *rx_i;          /* [Ns*sym_len] 2シンボルスライディング */
    RADE_COMP *rx_sym_td;     /* [M] EOO 用(周波数補正後の最新シンボルCP除去部) */
} rade_extract_v2_state;

/* 初期化。Wfwd は M*Nc の複素定数(行優先)。バッファは呼び出し側確保。 */
void rext_init(rade_extract_v2_state *st,
               int M, int Ncp, int Ns, int Nc, int latent_dim, float Fs,
               const RADE_COMP *Wfwd,
               RADE_COMP *rx_phase_vec, RADE_COMP *rx_i, RADE_COMP *rx_sym_td);

/* 1 modem frame(Ns シンボル)を抽出して z_hat(latent)を得る。
   rx_buf: リングバッファ(rade_rx_v2 の rx_buf、長さ 3*sym_len)
   delta_hat, freq_offset: rade_rx_v2 の同期推定値
   z_hat_out: [latent_dim] 出力(V2 は 1 modem frame = latent 1本)

   注: Python の receiver は複数 modem frame を扱えるが、V2 の逐次処理では
   rx_i = Ns*sym_len = 1 modem frame。よって z_hat は latent_dim 1本。 */
void rext_extract(rade_extract_v2_state *st,
                  const RADE_COMP *rx_buf,
                  float delta_hat, float freq_offset,
                  float *z_hat_out);

void rext_set_time_offset(rade_extract_v2_state *st,
                  int time_offset, int correct_time_offset);

#endif /* RADE_EXTRACT_V2_H */
