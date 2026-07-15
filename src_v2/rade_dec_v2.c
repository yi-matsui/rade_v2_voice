/*---------------------------------------------------------------------------*\

  rade_dec_v2.c

  RADE V2 stateful decoder。V1 nopy の rade_dec.c を V2 次元・層名に
  差し替えて移植。

  重要: Python の CoreDecoderStatefull.forward は各層の後に n() を適用する。
    def n(x): return clamp(x + (1/127)*(rand-0.5), -1, 1)
  推論では量子化ノイズ項(rand)は無視し、clamp[-1,1] のみが効く。
  V1 の rade_dec.c にはこの clamp が無かった(V1 は decoder 単体照合を
  していなかったため露見しなかった)。V2 では各層後に clamp を追加する。
  output 層の後には n() が無いのでクランプしない。

\*---------------------------------------------------------------------------*/

#include <string.h>
#include "nnet.h"
#include "rade_dec_v2.h"

#ifndef OPUS_CLEAR
#include <string.h>
#define OPUS_CLEAR(dst, n) (memset((dst), 0, (n)*sizeof(*(dst))))
#endif

/* Python の n(x) の推論時等価: clamp(x, -1, 1) を len 要素に適用 */
static void clamp_n(float *x, int len)
{
    int i;
    for (i = 0; i < len; i++) {
        if (x[i] < -1.0f) x[i] = -1.0f;
        else if (x[i] > 1.0f) x[i] = 1.0f;
    }
}

void rade_init_decoder_v2(RADEDecV2State *dec_state)
{
    memset(dec_state, 0, sizeof(*dec_state));
}

/* V1 と同一の conv1_cond_init(dilation ぶんクリア、呼び出し毎に init=1) */
static void conv1_cond_init(float *mem, int len, int dilation, int *init)
{
    if (!*init) {
        int i;
        for (i = 0; i < dilation; i++) OPUS_CLEAR(&mem[i*len], len);
    }
    *init = 1;
}

void rade_core_decoder_v2(
    RADEDecV2State  *dec_state,
    const RADEDecV2 *model,
    float           *features,
    const float     *latents,
    int arch
    )
{
    float buffer[DEC_V2_DENSE1_OUT_SIZE + DEC_V2_GRU1_OUT_SIZE + DEC_V2_GRU2_OUT_SIZE
                 + DEC_V2_GRU3_OUT_SIZE + DEC_V2_GRU4_OUT_SIZE + DEC_V2_GRU5_OUT_SIZE
                 + DEC_V2_CONV1_OUT_SIZE + DEC_V2_CONV2_OUT_SIZE + DEC_V2_CONV3_OUT_SIZE
                 + DEC_V2_CONV4_OUT_SIZE + DEC_V2_CONV5_OUT_SIZE];
    int output_index = 0;

    /* x = n(tanh(dense1(z))) */
    compute_generic_dense(&model->dec_v2_dense1, &buffer[output_index], latents, ACTIVATION_TANH, arch);
    clamp_n(&buffer[output_index], DEC_V2_DENSE1_OUT_SIZE);
    output_index += DEC_V2_DENSE1_OUT_SIZE;

    /* gru1 -> n(), glu1 -> n(), conv1 -> n() */
    /* gru1_state は recurrent state と兼用のため直接 clamp しない。
       Python の GRUStatefull.states は無clampのまま次フレームに引き継がれる
       (n() は呼び出し側の返り値コピーにのみ掛かる)。GLU 入力用のコピーだけ
       別バッファで clamp する。(s=28 features 1.5%差異の原因調査で確定) */
    {
        float gru1_glu_in[DEC_V2_GRU1_OUT_SIZE];
        compute_generic_gru(&model->dec_v2_gru1_input, &model->dec_v2_gru1_recurrent, dec_state->gru1_state, buffer, arch);
        memcpy(gru1_glu_in, dec_state->gru1_state, sizeof(gru1_glu_in));
        clamp_n(gru1_glu_in, DEC_V2_GRU1_OUT_SIZE);
        compute_glu(&model->dec_v2_glu1, &buffer[output_index], gru1_glu_in, arch);
    }
    clamp_n(&buffer[output_index], DEC_V2_GRU1_OUT_SIZE);
    output_index += DEC_V2_GRU1_OUT_SIZE;
    conv1_cond_init(dec_state->conv1_state, output_index, 1, &dec_state->initialized);
    compute_generic_conv1d(&model->dec_v2_conv1, &buffer[output_index], dec_state->conv1_state, buffer, output_index, ACTIVATION_TANH, arch);
    clamp_n(&buffer[output_index], DEC_V2_CONV1_OUT_SIZE);
    output_index += DEC_V2_CONV1_OUT_SIZE;

    /* gru2_state は recurrent state と兼用のため直接 clamp しない(gru1と同じ理由) */
    {
        float gru2_glu_in[DEC_V2_GRU2_OUT_SIZE];
        compute_generic_gru(&model->dec_v2_gru2_input, &model->dec_v2_gru2_recurrent, dec_state->gru2_state, buffer, arch);
        memcpy(gru2_glu_in, dec_state->gru2_state, sizeof(gru2_glu_in));
        clamp_n(gru2_glu_in, DEC_V2_GRU2_OUT_SIZE);
        compute_glu(&model->dec_v2_glu2, &buffer[output_index], gru2_glu_in, arch);
    }
    clamp_n(&buffer[output_index], DEC_V2_GRU2_OUT_SIZE);
    output_index += DEC_V2_GRU2_OUT_SIZE;
    conv1_cond_init(dec_state->conv2_state, output_index, 1, &dec_state->initialized);
    compute_generic_conv1d(&model->dec_v2_conv2, &buffer[output_index], dec_state->conv2_state, buffer, output_index, ACTIVATION_TANH, arch);
    clamp_n(&buffer[output_index], DEC_V2_CONV2_OUT_SIZE);
    output_index += DEC_V2_CONV2_OUT_SIZE;

    /* gru3_state は recurrent state と兼用のため直接 clamp しない(gru1と同じ理由) */
    {
        float gru3_glu_in[DEC_V2_GRU3_OUT_SIZE];
        compute_generic_gru(&model->dec_v2_gru3_input, &model->dec_v2_gru3_recurrent, dec_state->gru3_state, buffer, arch);
        memcpy(gru3_glu_in, dec_state->gru3_state, sizeof(gru3_glu_in));
        clamp_n(gru3_glu_in, DEC_V2_GRU3_OUT_SIZE);
        compute_glu(&model->dec_v2_glu3, &buffer[output_index], gru3_glu_in, arch);
    }
    clamp_n(&buffer[output_index], DEC_V2_GRU3_OUT_SIZE);
    output_index += DEC_V2_GRU3_OUT_SIZE;
    conv1_cond_init(dec_state->conv3_state, output_index, 1, &dec_state->initialized);
    compute_generic_conv1d(&model->dec_v2_conv3, &buffer[output_index], dec_state->conv3_state, buffer, output_index, ACTIVATION_TANH, arch);
    clamp_n(&buffer[output_index], DEC_V2_CONV3_OUT_SIZE);
    output_index += DEC_V2_CONV3_OUT_SIZE;

    /* gru4_state は recurrent state と兼用のため直接 clamp しない(gru1と同じ理由) */
    {
        float gru4_glu_in[DEC_V2_GRU4_OUT_SIZE];
        compute_generic_gru(&model->dec_v2_gru4_input, &model->dec_v2_gru4_recurrent, dec_state->gru4_state, buffer, arch);
        memcpy(gru4_glu_in, dec_state->gru4_state, sizeof(gru4_glu_in));
        clamp_n(gru4_glu_in, DEC_V2_GRU4_OUT_SIZE);
        compute_glu(&model->dec_v2_glu4, &buffer[output_index], gru4_glu_in, arch);
    }
    clamp_n(&buffer[output_index], DEC_V2_GRU4_OUT_SIZE);
    output_index += DEC_V2_GRU4_OUT_SIZE;
    conv1_cond_init(dec_state->conv4_state, output_index, 1, &dec_state->initialized);
    compute_generic_conv1d(&model->dec_v2_conv4, &buffer[output_index], dec_state->conv4_state, buffer, output_index, ACTIVATION_TANH, arch);
    clamp_n(&buffer[output_index], DEC_V2_CONV4_OUT_SIZE);
    output_index += DEC_V2_CONV4_OUT_SIZE;

    /* gru5_state は recurrent state と兼用のため直接 clamp しない(gru1と同じ理由) */
    {
        float gru5_glu_in[DEC_V2_GRU5_OUT_SIZE];
        compute_generic_gru(&model->dec_v2_gru5_input, &model->dec_v2_gru5_recurrent, dec_state->gru5_state, buffer, arch);
        memcpy(gru5_glu_in, dec_state->gru5_state, sizeof(gru5_glu_in));
        clamp_n(gru5_glu_in, DEC_V2_GRU5_OUT_SIZE);
        compute_glu(&model->dec_v2_glu5, &buffer[output_index], gru5_glu_in, arch);
    }
    clamp_n(&buffer[output_index], DEC_V2_GRU5_OUT_SIZE);
    output_index += DEC_V2_GRU5_OUT_SIZE;
    conv1_cond_init(dec_state->conv5_state, output_index, 1, &dec_state->initialized);
    compute_generic_conv1d(&model->dec_v2_conv5, &buffer[output_index], dec_state->conv5_state, buffer, output_index, ACTIVATION_TANH, arch);
    clamp_n(&buffer[output_index], DEC_V2_CONV5_OUT_SIZE);
    output_index += DEC_V2_CONV5_OUT_SIZE;

    /* output 層(この後に n() は無い) */
    compute_generic_dense(&model->dec_v2_output, features, buffer, ACTIVATION_LINEAR, arch);
}
