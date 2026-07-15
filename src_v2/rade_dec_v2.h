/*---------------------------------------------------------------------------*\

  rade_dec_v2.h

  RADE V2 stateful decoder。既存 nopy(V1)の rade_dec.c / rade_dec.h を、
  V2 の次元・層名に差し替えて移植したもの。

  forward 構造は V1 と完全に同一:
    dense1(TANH) → [gru_k → glu_k → conv_k(TANH)] × 5 → output(LINEAR)

  差し替えたのは以下のみ(ロジックは一字一句 V1 と同じ):
    型   RADEDec      -> RADEDecV2
    状態 RADEDecState -> RADEDecV2State
    層名 dec_*        -> dec_v2_*
    次元 DEC_*        -> DEC_V2_*   (GRU 96->128, conv state 等)

  対応 Python (radae_base.py): CoreDecoderStatefull
    latent_dim=56 入力 → features 84(= frames_per_step 4 × output_dim 21)出力

\*---------------------------------------------------------------------------*/

#ifndef RADE_DEC_V2_H
#define RADE_DEC_V2_H

#include "rade_dec_v2_data.h"   /* export 生成: RADEDecV2 と DEC_V2_* と init_radedecv2 */

/* V2 decoder の状態(V1 RADEDecStruct と同じ構成、次元だけ V2)。
   export は state 構造体を生成しないので、ここで定義する。 */
typedef struct RADEDecV2Struct {
    int   initialized;
    float gru1_state[DEC_V2_GRU1_STATE_SIZE];
    float gru2_state[DEC_V2_GRU2_STATE_SIZE];
    float gru3_state[DEC_V2_GRU3_STATE_SIZE];
    float gru4_state[DEC_V2_GRU4_STATE_SIZE];
    float gru5_state[DEC_V2_GRU5_STATE_SIZE];
    float conv1_state[DEC_V2_CONV1_STATE_SIZE];
    float conv2_state[DEC_V2_CONV2_STATE_SIZE];
    float conv3_state[DEC_V2_CONV3_STATE_SIZE];
    float conv4_state[DEC_V2_CONV4_STATE_SIZE];
    float conv5_state[DEC_V2_CONV5_STATE_SIZE];
} RADEDecV2State;

/* 状態初期化(memset)。resync 時にも呼ぶ。 */
void rade_init_decoder_v2(RADEDecV2State *dec_state);

/* V2 stateful decoder 1 回分。
   latents: [latent_dim=56]、features: [DEC_V2_OUTPUT_OUT_SIZE=84] 出力。
   84 = frames_per_step(4) × output_dim(21)。 */
void rade_core_decoder_v2(RADEDecV2State *dec_state, const RADEDecV2 *model,
                          float *features, const float *latents, int arch);

#endif /* RADE_DEC_V2_H */
