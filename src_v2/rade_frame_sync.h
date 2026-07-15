/*---------------------------------------------------------------------------*\

  rade_frame_sync.h

  RADE V2 フレーム同期ニューラルネット (FrameSyncNet) の C 実装。

  本家 V2 リファレンス models_sync.py の移植:

      Linear(input_dim, 64) -> ReLU
      Linear(64, 64)        -> ReLU
      Linear(64, 1)         -> Sigmoid

  V1 の DSP ベース acquisition (rade_acq.c) を置き換える、V2 の
  フレーム同期メトリック算出器。入力は latent 相当ベクトル az_hat[latent_dim]、
  出力は「今のフレーム位相が正しいか」を表す 0〜1 のスカラ。

  対応 Python:
    models_sync.py : class FrameSyncNet
    radae_v2.py    : self.frame_sync_nn(az_hat) 呼び出し部

\*---------------------------------------------------------------------------*/

#ifndef RADE_FRAME_SYNC_H
#define RADE_FRAME_SYNC_H

#include "nnet.h"

/* FrameSyncNet の中間層幅 (models_sync.py: w1 = 64) */
#define FSYNC_HIDDEN 64

/* FrameSyncNet モデル (3 つの全結合層) */
typedef struct {
    LinearLayer l1;   /* input_dim -> 64 */
    LinearLayer l2;   /* 64        -> 64 */
    LinearLayer l3;   /* 64        -> 1  */
    int input_dim;    /* = latent_dim (V2: 56) */
} FrameSyncNet;

/* 重み配列 (frame_sync_data.c で定義) からモデルを初期化する。
   arrays: parse_weights() 済みの WeightArray 配列
   input_dim: latent_dim (V2 では 56)
   戻り値: 0 = 成功, 非0 = いずれかの層の linear_init 失敗 */
int fsync_init(FrameSyncNet *m, const WeightArray *arrays, int input_dim);

/* 1 フレーム分の同期メトリックを算出する。
   az_hat: 入力ベクトル [input_dim]
   arch:   opus の SIMD arch 選択 (通常 0)
   戻り値: 0〜1 のメトリック (Sigmoid 出力) */
float fsync_forward(const FrameSyncNet *m, const float *az_hat, int arch);

#endif /* RADE_FRAME_SYNC_H */
