/*---------------------------------------------------------------------------*\

  rade_api_v2.h

  RADE V2 の全部品(受信6+送信2)を束ねた、DLL配布用の公開API。
  Python/torch/conda 不要、opus 静的リンクの単体 DLL として使える。

  対応する確定済み部品:
    受信: rade_rx_v2(DSP), rade_extract_v2, rade_eoo_v2, rade_frame_sync,
          rade_dec_v2, rade_sync_v2(統合)
    送信: rade_enc_v2, rade_tx_v2

  設計方針:
    - opaque ハンドル(RADEV2Context*)で全状態を隠蔽
    - C# から P/Invoke しやすいよう、フラットな引数(float*配列)を使う
    - 重み・定数(Wfwd/Winv/pend/各NN重み)は全部コンパイル時に埋め込み済み
      (export_rade_v2_weights.py / export_rade_v2_constants.py の生成物を
       静的リンク)。実行時に外部ファイルは一切不要。

\*---------------------------------------------------------------------------*/

#ifndef RADE_API_V2_H
#define RADE_API_V2_H

#include "rade_v2_comp.h"

#ifdef _WIN32
  #ifdef RADE_V2_BUILD_DLL
    #define RADE_V2_API __declspec(dllexport)
  #else
    #define RADE_V2_API __declspec(dllimport)
  #endif
#else
  #define RADE_V2_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct RADEV2Context RADEV2Context;

RADE_V2_API void rade_v2_set_limit_pitch(RADEV2Context *ctx, int enable);   /* 既定1 */
RADE_V2_API void rade_v2_set_mute(RADEV2Context *ctx, int enable);          /* 既定0 */
RADE_V2_API void rade_v2_set_bpf(RADEV2Context *ctx, int enable);           /* 既定1 */
RADE_V2_API void rade_v2_set_agc(RADEV2Context *ctx, int enable);           /* 既定1 */
RADE_V2_API void rade_v2_set_timing_adj(RADEV2Context *ctx, int enable);    /* 既定1 */
RADE_V2_API void rade_v2_set_time_offset(RADEV2Context *ctx,
                             int time_offset, int correct_time_offset);

/* ---- 生成・破棄 ---- */

/* コンテキストを生成し、全部品(重みロード込み)を初期化する。
   失敗時は NULL を返す。 */
RADE_V2_API RADEV2Context* rade_v2_open(void);

/* コンテキストを破棄する。 */
RADE_V2_API void rade_v2_close(RADEV2Context *ctx);

/* ---- 諸元取得(バッファサイズ決定用) ---- */

RADE_V2_API int rade_v2_n_features_in(RADEV2Context *ctx);   /* 送信1回の features 入力サイズ(frames_per_step*feature_dim=84) */
RADE_V2_API int rade_v2_n_tx_out(RADEV2Context *ctx);        /* 送信1回のIQ出力サンプル数(Ns*sym_len) */
RADE_V2_API int rade_v2_n_features_out(RADEV2Context *ctx);  /* 受信1回のfeatures出力サイズ(84) */
RADE_V2_API int rade_v2_sym_len(RADEV2Context *ctx);         /* 1シンボルのサンプル数(Ncp+M) */
RADE_V2_API int rade_v2_n_eoo_out(RADEV2Context *ctx);       /* EOO送信のIQサンプル数 */

/* ---- 送信 ---- */

/* 1 modem frame 送信。
   features_in: [rade_v2_n_features_in 個]
   tx_out:      [rade_v2_n_tx_out 個] の複素IQ(CP込み)を書き込む */
RADE_V2_API void rade_v2_tx(RADEV2Context *ctx, const float *features_in, RADE_COMP *tx_out);

/* EOO(End Of Over) 送信サンプルを取得(定数, NN不要)。
   eoo_out: [rade_v2_n_eoo_out 個] */
RADE_V2_API void rade_v2_tx_eoo(RADEV2Context *ctx, RADE_COMP *eoo_out);

/* ---- 受信 ---- */

/* 1 シンボル分の IQ を処理する(_process_symbol 相当)。
   rx_in: [*nin_io 個]の複素IQ入力。
   *nin_io: 呼び出し前は今回処理するサンプル数、呼び出し後は次回に
            必要なサンプル数に更新される(timing_adj によって変わりうる)。
   features_out: [rade_v2_n_features_out 個]。has_features が真のときのみ
                 有効な値が入る(FrameSyncNetのeven/odd勝者フレームのみ出力)。
   戻り値: 1=sync状態, 0=idle状態(受信機の現在状態)
   sig_det_out / sine_det_out / has_features_out: 0/1 で結果を返す(NULL可) */
RADE_V2_API int rade_v2_rx(RADEV2Context *ctx,
                          const RADE_COMP *rx_in, int *nin_io,
                          float *features_out, int *has_features_out,
                          int *sig_det_out, int *sine_det_out);

/* 受信状態をリセットし、idle 状態に戻す(新規復調開始時などに使用)。 */
RADE_V2_API void rade_v2_rx_reset(RADEV2Context *ctx);

#ifdef __cplusplus
}
#endif

#endif /* RADE_API_V2_H */
