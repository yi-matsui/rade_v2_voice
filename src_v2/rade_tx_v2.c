/*---------------------------------------------------------------------------*\

  rade_tx_v2.c

  RADE V2 送信部(RADEv2Transmitter.transmit_frame の移植)。

  bottleneck: V2 は **0(z_dense は線形、tanh なし)** が正しい。
  本家 tx2.py が明示的に bottleneck=0 でモデルを構築しており、
  Python CoreEncoder.forward は bottleneck==1 のときのみ z=tanh(z_dense(x))。
  V1 は 1 が正しかったため、移植時に 1 のまま残っていたのが 2026-07-09 に
  発見された重大バグ(長い発話で z が tanh 非線形域に入り局所的に歪む)。
  クリス氏実装 radc_enc_v2.c のコメントにも
  "V2: bottleneck=0, so z_dense is always linear" とある。

\*---------------------------------------------------------------------------*/

#include "rade_tx_v2.h"

void rtx_init(rade_tx_v2_state *st,
              int M, int Ncp, int Ns, int Nc, int latent_dim,
              int feature_dim, int frames_per_step,
              const RADE_COMP *Winv,
              RADEEncV2State *enc_state, const RADEEncV2 *enc_model, int arch)
{
    st->M = M; st->Ncp = Ncp; st->Ns = Ns; st->Nc = Nc;
    st->latent_dim = latent_dim; st->feature_dim = feature_dim;
    st->frames_per_step = frames_per_step;
    st->sym_len = Ncp + M;
    st->Winv = Winv;
    st->enc_state = enc_state;
    st->enc_model = enc_model;
    st->arch = arch;
}

void rtx_transmit_frame(rade_tx_v2_state *st, const float *features_in,
                        RADE_COMP *tx_out)
{
    int M = st->M, Ncp = st->Ncp, Ns = st->Ns, Nc = st->Nc;
    int latent_dim = st->latent_dim;
    int sidx, c, m, k;
    float z[64];   /* latent_dim(56) 分。余裕を持って 64。 */

    /* --- stateful encoder: features -> latent z --- */
    /* bottleneck=0: z_dense は線形(tx2.py と一致)。上部コメント参照。 */
    rade_core_encoder_v2(st->enc_state, st->enc_model, z, features_in, st->arch, 0);

    /* --- QPSK map: z[2k]=real, z[2k+1]=imag -> tx_sym[Ns][Nc] --- */
    /* z の並びは reshape(1,Ns,Nc) 前の平坦 latent_dim=Ns*Nc*2 実数。
       Python: tx_sym = z[::2]+1j*z[1::2] してから reshape(1,Ns,Nc)。
       つまり複素シンボルの通し番号 k(0..Ns*Nc-1) が (sidx,c) = (k/Nc, k%Nc)
       に対応する(row-major)。 */
    {
        RADE_COMP tx_sym[/*Ns*Nc*/ 64];
        k = 0;
        for (sidx = 0; sidx < Ns; sidx++) {
            for (c = 0; c < Nc; c++) {
                tx_sym[sidx*Nc + c] = rc_make(z[2*k], z[2*k+1]);
                k++;
            }
        }

        /* --- IDFT: tx[sidx][m] = sum_c tx_sym[sidx][c] * Winv[c][m] --- */
        /* Winv は Nc x M 行優先: Winv[c*M + m] */
        for (sidx = 0; sidx < Ns; sidx++) {
            for (m = 0; m < M; m++) {
                RADE_COMP acc = rc_make(0.0f, 0.0f);
                for (c = 0; c < Nc; c++) {
                    acc = rc_add(acc, rc_mul(tx_sym[sidx*Nc + c], st->Winv[c*M + m]));
                }
                /* CP挿入: tx_out[sidx*sym_len + Ncp + m] = acc */
                tx_out[sidx*st->sym_len + Ncp + m] = acc;
            }
            /* CP: 末尾 Ncp サンプルを先頭にコピー
               tx_cp[:Ncp] = tx[-Ncp:]  (tx は M サンプル、末尾Ncp個) */
            for (m = 0; m < Ncp; m++) {
                tx_out[sidx*st->sym_len + m] = tx_out[sidx*st->sym_len + Ncp + (M - Ncp) + m];
            }
        }
    }
    (void)latent_dim;
}
