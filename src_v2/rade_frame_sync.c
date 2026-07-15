/*---------------------------------------------------------------------------*\

  rade_frame_sync.c

  RADE V2 フレーム同期ニューラルネット (FrameSyncNet) の C 実装。
  models_sync.py の FrameSyncNet を移植したもの。

  Python 対応 (models_sync.py):
      self.linear_stack = nn.Sequential(
          nn.Linear(input_dim, w1),  nn.ReLU(),   # w1 = 64
          nn.Linear(w1, w1),         nn.ReLU(),
          nn.Linear(w1, 1),          nn.Sigmoid())
      def forward(self, x): return self.linear_stack(x)

  重みは rade_sync_data.c 内の WeightArray として供給される。
  層名の規約は本家 export_rade_v2_weights.py の命名に一致させる:
      "sync_dense1_bias" / "sync_dense1_weights_float"   (input_dim -> 64)
      "sync_dense2_bias" / "sync_dense2_weights_float"   (64        -> 64)
      "sync_dense3_bias" / "sync_dense3_weights_float"   (64        -> 1)
  ※ float 重み (量子化なし)。export_rade_v2_weights.py が quantize=False で吐く。

\*---------------------------------------------------------------------------*/

#include <math.h>
#include "rade_frame_sync.h"

int fsync_init(FrameSyncNet *m, const WeightArray *arrays, int input_dim)
{
    m->input_dim = input_dim;

    /* linear_init(layer, arrays,
                   bias, subias, weights_int8, float_weights,
                   weights_idx, diag, scale, nb_inputs, nb_outputs)
       float 重みのみ使用 → bias と float_weights 以外は NULL。
       enc_dense1 (rade_enc の init) と同じ非量子化パターン。 */

    if (linear_init(&m->l1, arrays,
                    "sync_dense1_bias", NULL, NULL, "sync_dense1_weights_float",
                    NULL, NULL, NULL,
                    input_dim, FSYNC_HIDDEN))
        return 1;

    if (linear_init(&m->l2, arrays,
                    "sync_dense2_bias", NULL, NULL, "sync_dense2_weights_float",
                    NULL, NULL, NULL,
                    FSYNC_HIDDEN, FSYNC_HIDDEN))
        return 2;

    if (linear_init(&m->l3, arrays,
                    "sync_dense3_bias", NULL, NULL, "sync_dense3_weights_float",
                    NULL, NULL, NULL,
                    FSYNC_HIDDEN, 1))
        return 3;

    return 0;
}

float fsync_forward(const FrameSyncNet *m, const float *az_hat, int arch)
{
    float h1[FSYNC_HIDDEN];
    float h2[FSYNC_HIDDEN];
    float y;

    /* Linear(input_dim,64) + ReLU */
    compute_generic_dense(&m->l1, h1, az_hat, ACTIVATION_RELU, arch);

    /* Linear(64,64) + ReLU */
    compute_generic_dense(&m->l2, h2, h1, ACTIVATION_RELU, arch);

    /* Linear(64,1) + Sigmoid */
    compute_generic_dense(&m->l3, &y, h2, ACTIVATION_SIGMOID, arch);

    return y;
}
