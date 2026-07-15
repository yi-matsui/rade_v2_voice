/*---------------------------------------------------------------------------*\

  rade_tx_v2.h

  RADE V2 送信部。radae_v2.py の RADEv2Transmitter を移植。

  対応 Python (radae_v2.py: RADEv2Transmitter.transmit_frame):
    z = core_encoder_statefull(features)             latent(56)
    tx_sym = z[::2] + 1j*z[1::2]                      QPSK map(28複素=Ns*Nc)
    tx_sym = reshape(tx_sym, (Ns, Nc))
    tx = tx_sym @ Winv                                IDFT: (Ns,Nc)@(Nc,M)->(Ns,M)
    CP挿入: tx_cp[:, Ncp:] = tx; tx_cp[:, :Ncp] = tx[:, -Ncp:]
    (オプション) SSB BPF                              ここでは非対応(後日)

  V2 は pilots 無し・time_offset 無しのため、V1 の送信より大幅に単純。
  Winv は定数(Nc x M 複素、行優先)。extract の Wfwd(M x Nc)とは向きが逆。

\*---------------------------------------------------------------------------*/

#ifndef RADE_TX_V2_H
#define RADE_TX_V2_H

#include "rade_v2_comp.h"
#include "rade_enc_v2.h"

typedef struct {
    int   M, Ncp, Ns, Nc, latent_dim, feature_dim, frames_per_step;
    int   sym_len;    /* Ncp + M */

    const RADE_COMP *Winv;   /* [Nc*M] 行優先。呼び出し側が保持(定数) */

    RADEEncV2State  *enc_state;
    const RADEEncV2 *enc_model;

    int arch;
} rade_tx_v2_state;

/* 初期化。Winv は Nc*M の複素定数(行優先)。 */
void rtx_init(rade_tx_v2_state *st,
              int M, int Ncp, int Ns, int Nc, int latent_dim,
              int feature_dim, int frames_per_step,
              const RADE_COMP *Winv,
              RADEEncV2State *enc_state, const RADEEncV2 *enc_model, int arch);

/* 1 modem frame 送信。
   features_in: [frames_per_step * feature_dim] (reshape済みの1本ベクトル)
   tx_out: [Ns * sym_len] 複素 IQ 出力(CP込み) */
void rtx_transmit_frame(rade_tx_v2_state *st, const float *features_in,
                        RADE_COMP *tx_out);

#endif /* RADE_TX_V2_H */
