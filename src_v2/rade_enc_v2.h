/*---------------------------------------------------------------------------*\

  rade_enc_v2.h

  RADE V2 stateful encoder。V1 nopy の rade_enc.c を V2 次元・層名に
  差し替えて移植。forward構造は V1 と同一(dilation の扱いも含む)。

  対応 Python (radae_base.py): CoreEncoderStatefull
    入力: features(frames_per_step=4, feature_dim=21) を1本の input_dim
          ベクトルに reshape してから処理(84 -> dense1)
    出力: latent_dim(56)。bottleneck=1 なら z=tanh(z_dense(x))

  Python forward の構造(decoderと同型、conv2〜5がdilation=2の点が違う):
    x = n(tanh(dense1(x)))
    x = cat([x, n(gru1(x))]); x = cat([x, n(conv1(x))])          dilation=1
    x = cat([x, n(gru2(x))]); x = cat([x, n(conv2(x))])          dilation=2
    x = cat([x, n(gru3(x))]); x = cat([x, n(conv3(x))])          dilation=2
    x = cat([x, n(gru4(x))]); x = cat([x, n(conv4(x))])          dilation=2
    x = cat([x, n(gru5(x))]); x = cat([x, n(conv5(x))])          dilation=2
    z = tanh(z_dense(x))   (bottleneck==1 の場合。output層にn()は無い)

  重要(decoder移植で判明した知見の適用):
    n(x) = clamp(x + rand項, -1, 1) の rand項は推論で無視、clamp[-1,1]のみ
    有効。GRU の recurrent state(gruN_state)自体は無clampのまま保持し、
    次段(conv/出力連結)に渡すコピーだけを clamp する
    (GRUStatefull.states は無clampで次フレームに引き継がれるため)。

\*---------------------------------------------------------------------------*/

#ifndef RADE_ENC_V2_H
#define RADE_ENC_V2_H

#include "rade_enc_v2_data.h"   /* export 生成: RADEEncV2, ENC_V2_*, init_radeencv2 */

/* V2 encoder の状態(V1 RADEEncStruct と同じ構成、次元だけ V2)。
   export は state 構造体を生成しないので、ここで定義する。 */
typedef struct RADEEncV2Struct {
    int   initialized;
    float gru1_state[ENC_V2_GRU1_STATE_SIZE];
    float gru2_state[ENC_V2_GRU2_STATE_SIZE];
    float gru3_state[ENC_V2_GRU3_STATE_SIZE];
    float gru4_state[ENC_V2_GRU4_STATE_SIZE];
    float gru5_state[ENC_V2_GRU5_STATE_SIZE];
    /* conv state サイズは export の ENC_V2_CONVn_STATE_SIZE マクロを使わず、
       opus nnet.c の compute_generic_conv1d_dilation の実装に基づき、
       「input_size(呼び出し時点の累積output_index) * dilation * (ksize-1)」
       (kernel_size=2 固定なので ksize-1=1、つまり input_size*dilation)で
       明示的に確保する。V1 decoder は全 dilation=1 だったため
       ENC/DEC の STATE_SIZE マクロが偶然 "×1" のままでも動いていたが、
       V2 encoder の dilation=2 conv ではこのマクロ値が不足しクラッシュした
       (2026-07-06 の実機クラッシュ調査で確定)。
       累積output_index: conv1前=128, conv2前=288, conv3前=448,
                          conv4前=608, conv5前=768(dense1=64,gru=64,conv=96)
       dilation: conv1=1, conv2〜5=2 */
    float conv1_state[128 * 1];   /* = ENC_V2_CONV1_STATE_SIZE 相当(dilation=1) */
    float conv2_state[288 * 2];   /* dilation=2 のため export値(288*1)の2倍必要 */
    float conv3_state[448 * 2];
    float conv4_state[608 * 2];
    float conv5_state[768 * 2];
} RADEEncV2State;

/* 状態初期化(memset)。 */
void rade_init_encoder_v2(RADEEncV2State *enc_state);

/* V2 stateful encoder 1 回分。
   input: [frames_per_step(4) * feature_dim(21) = 84] (reshape済みの1本ベクトル)
   latents: [latent_dim=56] 出力。
   bottleneck: 1 なら tanh 出力(通常はこちら)、0 なら線形出力。 */
void rade_core_encoder_v2(RADEEncV2State *enc_state, const RADEEncV2 *model,
                          float *latents, const float *input,
                          int arch, int bottleneck);

#endif /* RADE_ENC_V2_H */
