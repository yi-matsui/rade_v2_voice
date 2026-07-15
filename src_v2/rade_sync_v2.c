/*---------------------------------------------------------------------------*\

  rade_sync_v2.c

  RADE V2 受信 統合部。確定済み5部品を radae_v2.py の順序で束ねる。

  [2026-07-09 パッチ] 2点:

  (1) limit_pitch / mute を実装(本家 rx2.py 既定に準拠)
      - limit_pitch(既定ON): features[:,:,18].clamp_(min=-1.4)
        本家ヘルプ: "prevent synthesis pops with some speakers/channels"
      - mute(既定OFF): 信号喪失時 features[:,:,0] = -5
      ctx に limit_pitch / mute フィールドを追加(rade_sync_v2.h も要追加)。

  (2) **EOO平滑値のリセット先を修正(実バグ)**
      本家 radae_v2.py では eoo_smooth は Receiver の単一メンバで、
      _detect_eoo(278行)が更新し、_process_idle のsync遷移(193行)と
      _process_sync のEOO検出(241-242行)がリセットする。
      C では eoo_smooth が rade_rx_v2_state と rade_eoo_v2_state の
      **両方に存在**し、リセットは前者にだけ書かれていた。しかし
      reoo_detect() が実際に更新・判定に使うのは後者である。
      結果として **EOO平滑値が一度も 0 に戻らない**状態だった。
      症状: EOO検出でidleに落ちた直後、eoo_smooth が閾値付近に残るため、
      再syncした途端にまた誤EOO検出してidleへ戻る(受信が始まらない/
      断続する)。連続交信・ロング運用で顕在化するタイプの不具合。
      修正: リセットは ctx->eoo->eoo_smooth / eoo_corr に対して行う。
      rx->eoo_smooth は状態表示用のミラーとして値を写すだけにする。

\*---------------------------------------------------------------------------*/

#include <math.h>
#include <string.h>
#include "rade_sync_v2.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* radae_v2.py の eoo_smooth/eoo_count リセット(193行, 241-242行)に対応。
   実体は rade_eoo_v2_state 側。rx 側は表示用ミラー。 */
static void reset_eoo_state(rade_sync_v2_ctx *ctx)
{
    ctx->eoo->eoo_smooth = 0.0f;
    ctx->eoo->eoo_corr   = 0.0f;
    ctx->rx->eoo_smooth  = 0.0f;   /* 表示用ミラー */
    ctx->rx->eoo_count   = 0;
}

/* ------------------------------------------------------------------ */
/* _adjust_timing                                                      */
/* ------------------------------------------------------------------ */
int rsync_adjust_timing(rade_sync_v2_ctx *ctx, int nin)
{
    rade_rx_v2_state *rx = ctx->rx;
    int sym_len = rx->sym_len;
    int shift = sym_len / 4;
    int i;

    /* timing_adj 無効 or fix_delta_hat 指定なら何もしない */
    if (!rx->timing_adj || rx->fix_delta_hat)
        return nin;

    if (rx->delta_hat > 3 * sym_len / 4) {
        /* delta_hat を shift 減らし、Ry_smooth を巡回シフト(前方へ) */
        RADE_COMP tmp[/*shift*/ 512];   /* sym_len/4 <= 512 で十分 */
        rx->delta_hat -= (float)shift;
        for (i = 0; i < shift; i++) tmp[i] = rx->Ry_smooth[i];
        for (i = 0; i < sym_len - shift; i++) rx->Ry_smooth[i] = rx->Ry_smooth[i + shift];
        for (i = 0; i < shift; i++) rx->Ry_smooth[sym_len - shift + i] = tmp[i];
        nin = sym_len + shift;
    }
    if (rx->delta_hat < sym_len / 4) {
        RADE_COMP tmp[512];
        rx->delta_hat += (float)shift;
        for (i = 0; i < shift; i++) tmp[i] = rx->Ry_smooth[sym_len - shift + i];
        for (i = sym_len - shift - 1; i >= 0; i--) rx->Ry_smooth[i + shift] = rx->Ry_smooth[i];
        for (i = 0; i < shift; i++) rx->Ry_smooth[i] = tmp[i];
        nin = sym_len - shift;
    }
    return nin;
}

/* ------------------------------------------------------------------ */
/* _update_frame_sync_and_decode(_process_sync 内から呼ぶ)             */
/*   FrameSyncNet で metric 算出 -> s の偶奇で even/odd 平滑 ->         */
/*   勝った位相なら decoder を通して features、負けたら have=0          */
/* ------------------------------------------------------------------ */
static void update_frame_sync_and_decode(rade_sync_v2_ctx *ctx,
                                         const float *az_hat,
                                         int sig_det, int sine_det,
                                         float *features_out, int *have_features)
{
    rade_rx_v2_state *rx = ctx->rx;
    float gamma = RSYNC_BETA;
    float metric;
    int winning = 0;

    *have_features = 0;

    /* metric = FrameSyncNet(az_hat) */
    metric = fsync_forward(ctx->fsync, az_hat, ctx->arch);

    if (rx->s % 2) {
        rx->frame_sync_odd = gamma * rx->frame_sync_odd + (1.0f - gamma) * metric;
        winning = (rx->frame_sync_odd > rx->frame_sync_even);
    } else {
        rx->frame_sync_even = gamma * rx->frame_sync_even + (1.0f - gamma) * metric;
        winning = (rx->frame_sync_even > rx->frame_sync_odd);
    }

    if (winning) {
        /* 勝者 latent を保存し decoder を通す */
        memcpy(ctx->az_hat, az_hat, ctx->latent_dim * sizeof(float));
        rade_core_decoder_v2(ctx->dec_state, ctx->dec_model,
                             features_out, az_hat, ctx->arch);

        /* radae_v2.py _update_frame_sync_and_decode(decode直後、本家と同位置):
             if self.args.limit_pitch: features[:,:,18].clamp_(min=-1.4)
             if self.args.mute and (not sig_det or sine_det): features[:,:,0] = -5.
           limit_pitch は本家既定ON(合成ポップ防止)、mute は本家既定OFF。 */
        {
            int f;
            for (f = 0; f < ctx->frames_per_step; f++) {
                float *fv = &features_out[f * ctx->feature_dim];
                if (ctx->limit_pitch && fv[18] < -1.4f) fv[18] = -1.4f;
                if (ctx->mute && (!sig_det || sine_det)) fv[0] = -5.0f;
            }
        }
        *have_features = 1;
    }
}

/* ------------------------------------------------------------------ */
/* _process_sync                                                       */
/* ------------------------------------------------------------------ */
int rsync_process_sync(rade_sync_v2_ctx *ctx, int sig_det, int sine_det,
                       float *features_out, int *have_features)
{
    rade_rx_v2_state *rx = ctx->rx;
    int next_state = RRX_STATE_SYNC;
    float delta_phi;
    int i;

    *have_features = 0;

    /* IIR-track timing and frequency offset */
    delta_phi = rc_arg(rx->Ry_smooth[rx->delta_hat_g]);
    rx->freq_offset_g = -delta_phi * rx->Fs / (2.0f * (float)M_PI * (float)rx->M);
    rx->delta_hat   = RSYNC_BETA * rx->delta_hat   + (1.0f - RSYNC_BETA) * (float)rx->delta_hat_g;
    rx->freq_offset = RSYNC_BETA * rx->freq_offset + (1.0f - RSYNC_BETA) * rx->freq_offset_g;

    /* Sustained signal loss -> idle */
    if (!sig_det || sine_det) rx->count += 1;
    else                      rx->count  = 0;
    if (rx->count == rx->hangover) {
        next_state = RRX_STATE_IDLE;
        rx->count  = 0;
        rx->count1 = 0;
    }

    /* New/different signal -> re-acquire */
    rx->new_sig_delta_hat = (fabsf((float)rx->delta_hat_g - rx->delta_hat) > (float)rx->Ncp);
    rx->new_sig_f_hat     = (fabsf(rx->freq_offset_g - rx->freq_offset) > 5.0f);
    if (sig_det && (rx->new_sig_delta_hat || rx->new_sig_f_hat)) rx->count1 += 1;
    else                                                          rx->count1  = 0;
    if (rx->count1 == 5) {
        next_state = RRX_STATE_IDLE;
        rx->count  = 0;
        rx->count1 = 0;
    }

    /* Extract symbol(idle 遷移時も実行) -> az_hat(latent) */
    rext_extract(ctx->ext, rx->rx_buf, rx->delta_hat, rx->freq_offset, ctx->az_hat);

    /* End of over 検出。rx_sym_td は extract が更新済み。 */
    if (reoo_detect(ctx->eoo, ctx->ext->rx_sym_td)) {
        rx->count      = 0;
        rx->count1     = 0;
        /* radae_v2.py 241-242行: eoo_count = 0; eoo_smooth = 0.0
           (実体は rade_eoo_v2_state 側。rx側フィールドはミラー) */
        reset_eoo_state(ctx);
        /* Ry_smooth をリセット(即再sync防止) */
        for (i = 0; i < rx->sym_len; i++) rx->Ry_smooth[i] = rc_make(0.0f, 0.0f);
        *have_features = 0;
        return RRX_STATE_IDLE;
    }
    /* 表示用ミラーを更新(判定には使わない) */
    rx->eoo_smooth = ctx->eoo->eoo_smooth;

    /* FrameSyncNet で even/odd 選択 -> 勝者なら decoder */
    update_frame_sync_and_decode(ctx, ctx->az_hat, sig_det, sine_det,
                                 features_out, have_features);

    return next_state;
}

/* ------------------------------------------------------------------ */
/* _process_symbol(親)                                                 */
/* ------------------------------------------------------------------ */
int rsync_process_symbol(rade_sync_v2_ctx *ctx,
                         const RADE_COMP *rx_in, int *nin_io,
                         float *features_out, int *have_features,
                         int *sig_det_out, int *sine_det_out)
{
    rade_rx_v2_state *rx = ctx->rx;
    int nin = *nin_io;
    float gain;
    int sig_det = 0, sine_det = 0;
    int next_state;

    *have_features = 0;

    /* symbol counter を毎シンボル増やす(rx2.py 外側ループの receiver.s += 1 相当。
       状態に関係なく _process_symbol 呼び出しごとに増える)。 */
    rx->s += 1;

    /* gain -> buf -> autocorr -> detect(DSP 部, 確定済み) */
    gain = rrx_compute_gain(rx, rx_in, nin);
    rrx_update_rx_buf(rx, rx_in, nin, gain);
    nin = rx->sym_len;

    rrx_compute_autocorr(rx);
    rrx_detect_signal(rx, &sig_det, &sine_det);

    next_state = rx->state;

    if (rx->state == RRX_STATE_IDLE) {
        next_state = rrx_process_idle(rx, sig_det, sine_det);
        /* radae_v2.py 193行: sync遷移時に eoo_smooth = 0.0。
           rrx_process_idle は eoo 部品を知らないため、ここで実体をリセット。 */
        if (next_state == RRX_STATE_SYNC)
            reset_eoo_state(ctx);
    } else if (rx->state == RRX_STATE_SYNC) {
        next_state = rsync_process_sync(ctx, sig_det, sine_det,
                                        features_out, have_features);
        nin = rsync_adjust_timing(ctx, nin);
    }

    rx->state = next_state;
    *nin_io = nin;
    if (sig_det_out)  *sig_det_out  = sig_det;
    if (sine_det_out) *sine_det_out = sine_det;
    return next_state;
}
