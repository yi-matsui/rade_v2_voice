/*---------------------------------------------------------------------------*\

  rade_api_v2.c

  RADE V2 全部品を束ねる公開API(rade_api_v2.h)の実装。

  ここで束ねる部品と、それぞれの重み・定数の由来:
    - FrameSyncNet    : radesync_arrays[]     (rade_sync_data.c, export生成)
    - V2 decoder      : radedecv2_arrays[]    (rade_dec_v2_data.c, export生成)
    - V2 encoder      : radeencv2_arrays[]    (rade_enc_v2_data.c, export生成)
    - Wfwd/Winv/pend  : RADE_V2_WFWD/WINV/PEND(rade_v2_constants_data.c,
                        export_rade_v2_constants.py 生成)

  幾何パラメータ(V2固定): M=128, Ncp=32, Ns=2, Nc=14, latent_dim=56,
  feature_dim=21, frames_per_step=4。

  [2026-07-09 パッチ] 本家 rx2.py 既定仕様との機能一致:
    - RX入力BPF(rade_bpf_v2, 本家complex_bpf移植)を rade_v2_rx 入口に適用。
      本家rx2.py既定ON(--no_bpf で無効化)。
    - limit_pitch 既定ON(rade_sync_v2 側で適用。本家既定ON)。
    - mute 既定OFF(本家既定OFF)。
    - timing_adj: rx2.py は s > timing_adj_at(既定0)で有効化する。
      つまり実効的に最初のシンボルから有効。同じ挙動を既定とする。
    - time_offset(-16)/correct_time_offset(-8) は rade_extract_v2 側の
      既定になった(本家既定)。旧動作(0,0)は setter で戻せる。
    - 各機能の切替 setter を追加(A/B検証・旧基準との回帰照合用)。
  注意: AGC は本家rx2.py既定OFFだが、実運用(レベル不定の実入力)を考慮し
  既定ONのまま(クリス氏実装と同判断)。Python照合時は条件を明示的に
  揃えること(README_v2_spec_patch.md の対応表を参照)。

\*---------------------------------------------------------------------------*/

#define RADE_V2_BUILD_DLL
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "rade_api_v2.h"

#include "rade_rx_v2.h"
#include "rade_extract_v2.h"
#include "rade_eoo_v2.h"
#include "rade_frame_sync.h"
#include "rade_dec_v2.h"
#include "rade_enc_v2.h"
#include "rade_tx_v2.h"
#include "rade_sync_v2.h"
#include "rade_bpf_v2.h"
#include "rade_v2_constants_data.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* export 生成の重み配列(各 *_data.c 内で定義) */
extern const WeightArray radesync_arrays[];
extern const WeightArray radedecv2_arrays[];
extern const WeightArray radeencv2_arrays[];

/* V2 固定幾何(モデル定数。export生成のrade_v2_constants.hがあればそちらを
   優先すべきだが、ここでは自己完結のため直接定義する) */
#define V2_M            RADE_V2_CONST_M   /* 128 */
#define V2_NC           RADE_V2_CONST_NC  /* 14  */
#define V2_NCP          32
#define V2_NS           2
#define V2_LATENT_DIM   56
#define V2_FEATURE_DIM  21
#define V2_FRAMES_PER_STEP 4
#define V2_SYM_LEN      (V2_NCP + V2_M)
#define V2_FS           8000.0f

/* w[0]/w[Nc-1](= 2π*17/128, 2π*30/128)。モデル変更時は要更新
   (export_rade_v2_constants.py で model.w も出力するのが望ましい) */
#define V2_W_FIRST      0.834486f
#define V2_W_LAST       1.472622f

struct RADEV2Context {
    /* 受信側の状態一式 */
    rade_rx_v2_state       rx;
    RADE_COMP              rx_buf[3 * V2_SYM_LEN];
    RADE_COMP              rx_phase_vec[V2_SYM_LEN];
    RADE_COMP              Ry_norm[V2_SYM_LEN];
    RADE_COMP              Ry_smooth[V2_SYM_LEN];

    rade_extract_v2_state  ext;
    RADE_COMP              e_phase_vec[V2_SYM_LEN];
    RADE_COMP              e_rx_i[V2_NS * V2_SYM_LEN];
    RADE_COMP              e_sym_td[V2_M];

    rade_eoo_v2_state       eoo;

    FrameSyncNet            fsync;

    RADEDecV2               dec_model;
    RADEDecV2State          dec_state;

    rade_sync_v2_ctx        sync_ctx;

    /* RX入力BPF(本家rx2.py既定ON) */
    rade_bpf_v2_state       bpf;
    int                     bpf_en;

    /* timing_adj 有効化フラグ(rx2.py: s > timing_adj_at(既定0)で有効) */
    int                     timing_adj_enable;

    /* 送信側の状態一式 */
    RADEEncV2               enc_model;
    RADEEncV2State          enc_state;
    rade_tx_v2_state        tx;

    int opened_ok;
};

static void bpf_setup(RADEV2Context *ctx)
{
    /* rx2.py: Ntap=101,
       bandwidth = 1.2*(w[Nc-1]-w[0])*Fs/(2π), centre = (w[Nc-1]+w[0])*Fs/(2π)/2 */
    float bw = 1.2f * (V2_W_LAST - V2_W_FIRST) * V2_FS / (2.0f * (float)M_PI);
    float fc = (V2_W_LAST + V2_W_FIRST) * V2_FS / (2.0f * (float)M_PI) / 2.0f;
    rbpf_init(&ctx->bpf, 101, V2_FS, bw, fc);
}

RADEV2Context* rade_v2_open(void)
{
    RADEV2Context *ctx = (RADEV2Context*)calloc(1, sizeof(RADEV2Context));
    if (!ctx) return NULL;

    /* --- 受信側初期化 --- */
    /* w_first/w_last は BPF帯域/SNR推定にのみ使う。V2の角周波数範囲は
       Wfwd生成時のモデルに固定されているため、代表的な範囲を直接与える
       (SNR推定値の精度にのみ影響し、state/features の正しさには影響しない)。 */
    rrx_init(&ctx->rx, V2_M, V2_NCP, V2_NS, V2_NC, V2_FS,
             V2_W_FIRST, V2_W_LAST, 1 /*agc*/,
             ctx->rx_buf, ctx->rx_phase_vec, ctx->Ry_norm, ctx->Ry_smooth);
    ctx->rx.hangover = 75;   /* radae_v2.py Args.hangover のデフォルト */

    rext_init(&ctx->ext, V2_M, V2_NCP, V2_NS, V2_NC, V2_LATENT_DIM, V2_FS,
              RADE_V2_WFWD, ctx->e_phase_vec, ctx->e_rx_i, ctx->e_sym_td);
    /* rext_init が time_offset=-16 / correct_time_offset=-8(本家既定)を設定 */

    if (reoo_init(&ctx->eoo, V2_M, V2_NCP, RADE_V2_PEND) != 0) {
        free(ctx); return NULL;
    }

    if (fsync_init(&ctx->fsync, radesync_arrays, V2_LATENT_DIM) != 0) {
        free(ctx); return NULL;
    }

    if (init_radedecv2(&ctx->dec_model, radedecv2_arrays) != 0) {
        free(ctx); return NULL;
    }
    rade_init_decoder_v2(&ctx->dec_state);

    ctx->sync_ctx.rx = &ctx->rx;
    ctx->sync_ctx.ext = &ctx->ext;
    ctx->sync_ctx.eoo = &ctx->eoo;
    ctx->sync_ctx.fsync = &ctx->fsync;
    ctx->sync_ctx.dec_model = &ctx->dec_model;
    ctx->sync_ctx.dec_state = &ctx->dec_state;
    ctx->sync_ctx.latent_dim = V2_LATENT_DIM;
    ctx->sync_ctx.feature_dim = V2_FEATURE_DIM;
    ctx->sync_ctx.frames_per_step = V2_FRAMES_PER_STEP;
    ctx->sync_ctx.output_size = V2_FEATURE_DIM * V2_FRAMES_PER_STEP;
    ctx->sync_ctx.arch = 0;
    ctx->sync_ctx.limit_pitch = 1;   /* 本家rx2.py既定ON(合成ポップ防止) */
    ctx->sync_ctx.mute = 0;          /* 本家rx2.py既定OFF */

    /* RX入力BPF(本家rx2.py既定ON) */
    bpf_setup(ctx);
    ctx->bpf_en = 1;

    /* timing_adj(rx2.py: timing_adj_at=0 既定 → 実効的に最初から有効) */
    ctx->timing_adj_enable = 1;

    /* --- 送信側初期化 --- */
    if (init_radeencv2(&ctx->enc_model, radeencv2_arrays) != 0) {
        free(ctx); return NULL;
    }
    rade_init_encoder_v2(&ctx->enc_state);

    rtx_init(&ctx->tx, V2_M, V2_NCP, V2_NS, V2_NC, V2_LATENT_DIM,
             V2_FEATURE_DIM, V2_FRAMES_PER_STEP,
             RADE_V2_WINV, &ctx->enc_state, &ctx->enc_model, 0 /*arch*/);

    ctx->opened_ok = 1;
    return ctx;
}

void rade_v2_close(RADEV2Context *ctx)
{
    if (ctx) {
        reoo_free(&ctx->eoo);
        free(ctx);
    }
}

int rade_v2_n_features_in(RADEV2Context *ctx)
{
    (void)ctx;
    return V2_FRAMES_PER_STEP * V2_FEATURE_DIM;   /* 84 */
}

int rade_v2_n_tx_out(RADEV2Context *ctx)
{
    (void)ctx;
    return V2_NS * V2_SYM_LEN;
}

int rade_v2_n_features_out(RADEV2Context *ctx)
{
    (void)ctx;
    return V2_FEATURE_DIM * V2_FRAMES_PER_STEP;   /* 84 */
}

int rade_v2_sym_len(RADEV2Context *ctx)
{
    (void)ctx;
    return V2_SYM_LEN;
}

int rade_v2_n_eoo_out(RADEV2Context *ctx)
{
    (void)ctx;
    return RADE_V2_EOO_LEN;   /* export_rade_v2_constants.py 生成の定数長 */
}

void rade_v2_tx(RADEV2Context *ctx, const float *features_in, RADE_COMP *tx_out)
{
    rtx_transmit_frame(&ctx->tx, features_in, tx_out);
}

void rade_v2_tx_eoo(RADEV2Context *ctx, RADE_COMP *eoo_out)
{
    /* model.eoo_v2(定数)をそのまま返す。radae_v2.py の eoo() と対応。 */
    memcpy(eoo_out, RADE_V2_EOO, sizeof(RADE_COMP) * RADE_V2_EOO_LEN);
    (void)ctx;
}

int rade_v2_rx(RADEV2Context *ctx,
              const RADE_COMP *rx_in, int *nin_io,
              float *features_out, int *has_features_out,
              int *sig_det_out, int *sine_det_out)
{
    int have = 0, sig_det = 0, sine_det = 0;
    int next_state;

    /* RX入力BPF(rx2.py は receiver 前にストリーム全体へ適用。
       ブロック処理でも状態引き継ぎにより数値等価: test_bpf_v2 で検証済み)。
       AGC・acquisition はフィルタ後のサンプルを見る(本家と同順序)。 */
    RADE_COMP filt[RADE_BPF_V2_MAXLEN];
    const RADE_COMP *in_s = rx_in;
    if (ctx->bpf_en && *nin_io > 0 && *nin_io <= RADE_BPF_V2_MAXLEN) {
        rbpf_process(&ctx->bpf, filt, rx_in, *nin_io);
        in_s = filt;
    }

    next_state = rsync_process_symbol(&ctx->sync_ctx, in_s, nin_io,
                                      features_out, &have,
                                      &sig_det, &sine_det);

    /* rx2.py 外側ループ: if receiver.s > args.timing_adj_at: timing_adj = 1
       (timing_adj_at 既定0 → s>=1 で有効化) */
    if (ctx->timing_adj_enable && ctx->rx.s > 0)
        ctx->rx.timing_adj = 1;

    if (has_features_out) *has_features_out = have;
    if (sig_det_out)      *sig_det_out      = sig_det;
    if (sine_det_out)     *sine_det_out     = sine_det;
    return (next_state == RRX_STATE_SYNC) ? 1 : 0;
}

void rade_v2_rx_reset(RADEV2Context *ctx)
{
    int to, cto;

    /* rx の状態機械と各種カウンタをidleへリセット。
       rrx_init を呼び直すことで、バッファも含めて安全にゼロクリアされる。
       Wfwd/pend等の定数ポインタは不変なので extract/eoo の再確保は不要。 */
    rrx_init(&ctx->rx, V2_M, V2_NCP, V2_NS, V2_NC, V2_FS,
             V2_W_FIRST, V2_W_LAST, ctx->rx.agc_en,
             ctx->rx_buf, ctx->rx_phase_vec, ctx->Ry_norm, ctx->Ry_smooth);
    ctx->rx.hangover = 75;
    ctx->rx.timing_adj = 0;   /* 次の rade_v2_rx でフラグに応じ再有効化 */
    rade_init_decoder_v2(&ctx->dec_state);

    /* extract の位相・rx_i もクリア(time_offset 設定は保存して引き継ぐ) */
    to  = ctx->ext.time_offset;
    cto = ctx->ext.correct_time_offset;
    rext_init(&ctx->ext, V2_M, V2_NCP, V2_NS, V2_NC, V2_LATENT_DIM, V2_FS,
              RADE_V2_WFWD, ctx->e_phase_vec, ctx->e_rx_i, ctx->e_sym_td);
    rext_set_time_offset(&ctx->ext, to, cto);

    /* EOO平滑とBPF状態もクリア(新しいストリームの先頭として扱う) */
    ctx->eoo.eoo_smooth = 0.0f;
    rbpf_reset(&ctx->bpf);
}

/* --- 機能切替 setter(A/B検証・旧基準との回帰照合用) --- */

void rade_v2_set_limit_pitch(RADEV2Context *ctx, int enable)
{
    ctx->sync_ctx.limit_pitch = enable ? 1 : 0;
}

void rade_v2_set_mute(RADEV2Context *ctx, int enable)
{
    ctx->sync_ctx.mute = enable ? 1 : 0;
}

void rade_v2_set_bpf(RADEV2Context *ctx, int enable)
{
    /* 再有効化時はフィルタ状態をクリアしてから */
    if (enable && !ctx->bpf_en) rbpf_reset(&ctx->bpf);
    ctx->bpf_en = enable ? 1 : 0;
}

void rade_v2_set_agc(RADEV2Context *ctx, int enable)
{
    ctx->rx.agc_en = enable ? 1 : 0;
}

void rade_v2_set_timing_adj(RADEV2Context *ctx, int enable)
{
    ctx->timing_adj_enable = enable ? 1 : 0;
    if (!enable) ctx->rx.timing_adj = 0;
}

void rade_v2_set_time_offset(RADEV2Context *ctx,
                             int time_offset, int correct_time_offset)
{
    rext_set_time_offset(&ctx->ext, time_offset, correct_time_offset);
}
