/*---------------------------------------------------------------------------*\

  rade_enc_v2.c

  RADE V2 stateful encoder。V1 nopy の rade_enc.c を V2 次元・層名に
  差し替えて移植。decoder移植で判明した2つの知見を最初から適用する:
    (1) 各層出力に n()=clamp[-1,1] を掛ける(V1のOPUS_COPYには無かった)
    (2) GRU の recurrent state(gruN_state)は無clampのまま保持し、
        後段への入力用コピーだけ別バッファで clamp する

\*---------------------------------------------------------------------------*/

#include <string.h>
#include "nnet.h"
#include "rade_enc_v2.h"

#ifndef OPUS_CLEAR
#define OPUS_CLEAR(dst, n) (memset((dst), 0, (n)*sizeof(*(dst))))
#endif

/* Python の n(x) の推論時等価: clamp(x, -1, 1) */
static void clamp_n(float *x, int len)
{
    int i;
    for (i = 0; i < len; i++) {
        if (x[i] < -1.0f) x[i] = -1.0f;
        else if (x[i] > 1.0f) x[i] = 1.0f;
    }
}

void rade_init_encoder_v2(RADEEncV2State *enc_state)
{
    memset(enc_state, 0, sizeof(*enc_state));
}

/* V1 と同一(dilation ぶんクリア、呼び出し毎に init=1) */
static void conv1_cond_init(float *mem, int len, int dilation, int *init)
{
    if (!*init) {
        int i;
        for (i = 0; i < dilation; i++) OPUS_CLEAR(&mem[i*len], len);
    }
    *init = 1;
}

void rade_core_encoder_v2(RADEEncV2State *enc_state, const RADEEncV2 *model,
                          float *latents, const float *input,
                          int arch, int bottleneck)
{
    float buffer[ENC_V2_DENSE1_OUT_SIZE + ENC_V2_GRU1_OUT_SIZE + ENC_V2_GRU2_OUT_SIZE
               + ENC_V2_GRU3_OUT_SIZE + ENC_V2_GRU4_OUT_SIZE + ENC_V2_GRU5_OUT_SIZE
               + ENC_V2_CONV1_OUT_SIZE + ENC_V2_CONV2_OUT_SIZE + ENC_V2_CONV3_OUT_SIZE
               + ENC_V2_CONV4_OUT_SIZE + ENC_V2_CONV5_OUT_SIZE];
    int output_index = 0;
    int activation;

    /* x = n(tanh(dense1(x))) */
    compute_generic_dense(&model->enc_v2_dense1, &buffer[output_index], input, ACTIVATION_TANH, arch);
    clamp_n(&buffer[output_index], ENC_V2_DENSE1_OUT_SIZE);
    output_index += ENC_V2_DENSE1_OUT_SIZE;

    /* gru1(dilation=1) -> n(), conv1(dilation=1) -> n() */
    {
        float gru1_glu_in[ENC_V2_GRU1_OUT_SIZE];
        compute_generic_gru(&model->enc_v2_gru1_input, &model->enc_v2_gru1_recurrent, enc_state->gru1_state, buffer, arch);
        memcpy(gru1_glu_in, enc_state->gru1_state, sizeof(gru1_glu_in));
        clamp_n(gru1_glu_in, ENC_V2_GRU1_OUT_SIZE);
        memcpy(&buffer[output_index], gru1_glu_in, sizeof(gru1_glu_in));
    }
    output_index += ENC_V2_GRU1_OUT_SIZE;
    conv1_cond_init(enc_state->conv1_state, output_index, 1, &enc_state->initialized);
    compute_generic_conv1d(&model->enc_v2_conv1, &buffer[output_index], enc_state->conv1_state, buffer, output_index, ACTIVATION_TANH, arch);
    clamp_n(&buffer[output_index], ENC_V2_CONV1_OUT_SIZE);
    output_index += ENC_V2_CONV1_OUT_SIZE;

    /* gru2(dilation=2) -> n(), conv2(dilation=2) -> n() */
    {
        float gru2_glu_in[ENC_V2_GRU2_OUT_SIZE];
        compute_generic_gru(&model->enc_v2_gru2_input, &model->enc_v2_gru2_recurrent, enc_state->gru2_state, buffer, arch);
        memcpy(gru2_glu_in, enc_state->gru2_state, sizeof(gru2_glu_in));
        clamp_n(gru2_glu_in, ENC_V2_GRU2_OUT_SIZE);
        memcpy(&buffer[output_index], gru2_glu_in, sizeof(gru2_glu_in));
    }
    output_index += ENC_V2_GRU2_OUT_SIZE;
    conv1_cond_init(enc_state->conv2_state, output_index, 2, &enc_state->initialized);
    compute_generic_conv1d_dilation(&model->enc_v2_conv2, &buffer[output_index], enc_state->conv2_state, buffer, output_index, 2, ACTIVATION_TANH, arch);
    clamp_n(&buffer[output_index], ENC_V2_CONV2_OUT_SIZE);
    output_index += ENC_V2_CONV2_OUT_SIZE;

    /* gru3(dilation=2) -> n(), conv3(dilation=2) -> n() */
    {
        float gru3_glu_in[ENC_V2_GRU3_OUT_SIZE];
        compute_generic_gru(&model->enc_v2_gru3_input, &model->enc_v2_gru3_recurrent, enc_state->gru3_state, buffer, arch);
        memcpy(gru3_glu_in, enc_state->gru3_state, sizeof(gru3_glu_in));
        clamp_n(gru3_glu_in, ENC_V2_GRU3_OUT_SIZE);
        memcpy(&buffer[output_index], gru3_glu_in, sizeof(gru3_glu_in));
    }
    output_index += ENC_V2_GRU3_OUT_SIZE;
    conv1_cond_init(enc_state->conv3_state, output_index, 2, &enc_state->initialized);
    compute_generic_conv1d_dilation(&model->enc_v2_conv3, &buffer[output_index], enc_state->conv3_state, buffer, output_index, 2, ACTIVATION_TANH, arch);
    clamp_n(&buffer[output_index], ENC_V2_CONV3_OUT_SIZE);
    output_index += ENC_V2_CONV3_OUT_SIZE;

    /* gru4(dilation=2) -> n(), conv4(dilation=2) -> n() */
    {
        float gru4_glu_in[ENC_V2_GRU4_OUT_SIZE];
        compute_generic_gru(&model->enc_v2_gru4_input, &model->enc_v2_gru4_recurrent, enc_state->gru4_state, buffer, arch);
        memcpy(gru4_glu_in, enc_state->gru4_state, sizeof(gru4_glu_in));
        clamp_n(gru4_glu_in, ENC_V2_GRU4_OUT_SIZE);
        memcpy(&buffer[output_index], gru4_glu_in, sizeof(gru4_glu_in));
    }
    output_index += ENC_V2_GRU4_OUT_SIZE;
    conv1_cond_init(enc_state->conv4_state, output_index, 2, &enc_state->initialized);
    compute_generic_conv1d_dilation(&model->enc_v2_conv4, &buffer[output_index], enc_state->conv4_state, buffer, output_index, 2, ACTIVATION_TANH, arch);
    clamp_n(&buffer[output_index], ENC_V2_CONV4_OUT_SIZE);
    output_index += ENC_V2_CONV4_OUT_SIZE;

    /* gru5(dilation=2) -> n(), conv5(dilation=2) -> n() */
    {
        float gru5_glu_in[ENC_V2_GRU5_OUT_SIZE];
        compute_generic_gru(&model->enc_v2_gru5_input, &model->enc_v2_gru5_recurrent, enc_state->gru5_state, buffer, arch);
        memcpy(gru5_glu_in, enc_state->gru5_state, sizeof(gru5_glu_in));
        clamp_n(gru5_glu_in, ENC_V2_GRU5_OUT_SIZE);
        memcpy(&buffer[output_index], gru5_glu_in, sizeof(gru5_glu_in));
    }
    output_index += ENC_V2_GRU5_OUT_SIZE;
    conv1_cond_init(enc_state->conv5_state, output_index, 2, &enc_state->initialized);
    compute_generic_conv1d_dilation(&model->enc_v2_conv5, &buffer[output_index], enc_state->conv5_state, buffer, output_index, 2, ACTIVATION_TANH, arch);
    clamp_n(&buffer[output_index], ENC_V2_CONV5_OUT_SIZE);
    output_index += ENC_V2_CONV5_OUT_SIZE;

    /* z = tanh(z_dense(x)) (bottleneck==1)。output層にn()は無い。 */
    activation = (bottleneck == 1) ? ACTIVATION_TANH : ACTIVATION_LINEAR;
    compute_generic_dense(&model->enc_v2_zdense, latents, buffer, activation, arch);
}
