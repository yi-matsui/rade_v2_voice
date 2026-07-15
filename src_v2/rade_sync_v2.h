/*---------------------------------------------------------------------------*\

  rade_sync_v2.h

  RADE V2 受信 統合部。radae_v2.py の以下を移植し、確定済み5部品を束ねる:
    _process_symbol            -> rsync_process_symbol(親)
    _process_sync              -> rsync_process_sync
    _update_frame_sync_and_decode -> (rsync_process_sync 内)
    _adjust_timing             -> rsync_adjust_timing

  束ねる部品(すべて数値確定済み):
    rade_rx_v2     : gain/buf/autocorr/detect/idle(DSP部)
    rade_extract_v2: _extract_symbol(latent 取得)
    rade_eoo_v2    : _detect_eoo(EOO 検出)
    rade_frame_sync: FrameSyncNet(even/odd 位相選択)
    rade_dec_v2    : stateful decoder(latent -> features)

  流れ(sync 状態):
    autocorr/detect(DSP) -> _process_sync:
      IIR追従 -> loss/new-sig判定 -> extract(latent) -> EOO判定
      -> FrameSyncNet で even/odd 選択 -> 勝者 latent を decoder -> features

\*---------------------------------------------------------------------------*/

#ifndef RADE_SYNC_V2_H
#define RADE_SYNC_V2_H

#include "rade_rx_v2.h"
#include "rade_extract_v2.h"
#include "rade_eoo_v2.h"
#include "rade_frame_sync.h"
#include "rade_dec_v2.h"

/* 統合コンテキスト: DSP 状態(rade_rx_v2_state)に加え、各部品の状態を保持。 */
typedef struct {
    rade_rx_v2_state       *rx;      /* DSP + 状態機械(gain/buf/autocorr/detect/idle) */
    rade_extract_v2_state  *ext;     /* _extract_symbol */
    rade_eoo_v2_state      *eoo;     /* _detect_eoo */
    const FrameSyncNet     *fsync;   /* FrameSyncNet(重みは呼び出し側 init 済み) */
    const RADEDecV2        *dec_model;   /* decoder 重み */
    RADEDecV2State         *dec_state;   /* decoder stateful 状態 */

    int   latent_dim;   /* 56 */
    int   feature_dim;  /* 21 */
    int   frames_per_step; /* 4 */
    int   output_size;  /* 84 = frames_per_step * feature_dim */

    /* 直近の latent(az_hat)。勝者フレームで decoder に渡す。 */
    float az_hat[64];   /* latent_dim(56) 分。余裕を持って 64。 */

    int   arch;         /* opus SIMD arch(通常 0) */

	int limit_pitch;   /* 本家既定ON: feat[18] を -1.4 で下限クリップ */
    int mute;          /* 本家既定OFF: 信号喪失時 feat[0] = -5 */
} rade_sync_v2_ctx;

/* IIR / 判定定数(radae_v2.py と同名・同値) */
#define RSYNC_BETA      0.999f   /* delta_hat/freq/frame_sync の IIR */
/* hangover は ctx->rx->hangover に格納(args 由来) */

/* 1 シンボル処理(親, _process_symbol 相当)。
   rx_in[nin]: 入力 IQ。処理後の nin を *nin_io に返す。
   features_out[output_size]: 勝者フレームなら features、そうでなければ触らない。
   *have_features: 1 なら features_out に有効な出力あり。
   戻り値: 次状態(RRX_STATE_*)。 */
int rsync_process_symbol(rade_sync_v2_ctx *ctx,
                         const RADE_COMP *rx_in, int *nin_io,
                         float *features_out, int *have_features,
                         int *sig_det_out, int *sine_det_out);

/* sync 状態処理(_process_sync 相当)。features_out に勝者 features。 */
int rsync_process_sync(rade_sync_v2_ctx *ctx, int sig_det, int sine_det,
                       float *features_out, int *have_features);

/* タイミング調整(_adjust_timing 相当)。戻り値: 更新後 nin。 */
int rsync_adjust_timing(rade_sync_v2_ctx *ctx, int nin);

#endif /* RADE_SYNC_V2_H */
