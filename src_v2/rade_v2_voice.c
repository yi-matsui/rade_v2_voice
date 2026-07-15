/*---------------------------------------------------------------------------*\

  rade_v2_voice.c

  RadeCallTest の rade_voice.dll(クリス氏 radc=GPL)を pure-C + FARGAN(BSD)で
  置換するための rv_* インターフェース実装。dumpbin /exports rade_voice.dll で
  確認した19関数と同じシグネチャを提供する(RadeNative.cs の P/Invoke に一致)。

  【第1版のスコープ】
    - RX 側の骨格のみ(create/destroy/set_gain + 状態取得 + push_modem仮実装)
    - V2 のみ(v2=1)。V1(DR-NOPY)は次段階。
    - push_modem の中身([1]ヒルベルト→[2]rade_v2_rx→[3]FARGAN)は次段階。
      まず「作って・状態取得して・壊す」枠が動くことを確認する。

  【インターフェース(RadeNative.cs / dumpbin と一致、全て __cdecl)】
    void*  rv_rx_create(int v2)
    void   rv_rx_destroy(void* h)
    void   rv_rx_set_gain(void* h, float gain)
    int    rv_rx_push_modem(void* h, float* modem8k, int n8, float* speech16kOut, int maxOut)
    int    rv_rx_synced(void* h)
    int    rv_rx_snr_db(void* h)
    float  rv_rx_freq_offset(void* h)
    int    rv_rx_get_callsign(void* h, unsigned char* dst, int max)
    int    rv_rx_callsign_seq(void* h)
    int    rv_rx_protocol(void* h)
    void   rv_global_init(void) / rv_global_shutdown(void)

  【V2 の状態取得について】
    rade_v2_rx は sig_det_out / sine_det_out を返すが SNR/foff は返さない。
    よって rv_rx_snr_db / rv_rx_freq_offset は V2 では 0 を返す(手段なし)。
    sync は sig_det をキャッシュして rv_rx_synced で返す。
    (dual-watch の V1 側は rade_snrdB_3k_est で SNR が出るので実用上は補える)

\*---------------------------------------------------------------------------*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 2026-07-14: RadeCallTestはコンソールを持たないGUIアプリのため、
   fprintf(stderr,...)は今までどこにも表示されていなかった(見えない
   ログを頼りに調査していたのがそもそもの見落とし)。OutputDebugStringA
   に切り替え、Sysinternals DebugView 等で実行中に観察できるようにする。 */
#ifdef _WIN32
#include <windows.h>
static void rv_dbg(const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    OutputDebugStringA(buf);
}
#else
#define rv_dbg(...) fprintf(stderr, __VA_ARGS__)
#endif

/* エクスポート指定(rade_voice.dll と同じく __declspec(dllexport)) */
#if defined(_WIN32)
  #define RV_API __declspec(dllexport)
#else
  #define RV_API
#endif

/* COMP 型はヒルベルト係数(ht_coeff.h)が要求する。RADE_COMP と同型。
   ht_coeff.h の include より前に定義が必要。 */
typedef struct { float real; float imag; } COMP;

#include "rade_api_v2.h"   /* RADEV2Context, rade_v2_open/close/rx ... */
/* V1(DR-NOPY)本体。RADE_COMPの型ガードが __RADE_COMP__ で統一されている前提
   (rade_v2_comp.h も同じガード名を使っていれば二重定義エラーにならない)。
   もしビルドでRADE_COMP再定義エラーが出たら、ここのincludeより前に
   rade_v2_comp.h 側の実際のガード名を確認する必要がある。 */
#include "rade_api.h"
/* NB_FEATURES は lpcnet.h から。fargan.h と同じ include 群を揃える
   (test_fargan.c で確認済みの構成)。 */
#include "arch.h"
#include "lpcnet.h"        /* NB_FEATURES=20 */
#include "freq.h"
#include "os_support.h"    /* OPUS_COPY */
#include "fargan.h"        /* FARGANState, fargan_init/cont/synthesize */
#include "cpu_support.h"   /* opus_select_arch(), TX側のLPCNet特徴抽出で使用 */
#include "ht_coeff.h"      /* HT_N=257, COMP ht_coeff[HT_N] */

/* ヒルベルト状態バッファ長。実装は1サンプル粒度の逐次処理(rv_hilbert_push)
   なので、実際に使うのは htbuf[0..HT_N] のみ(HT_N+1個)。
   (2026-07-13: 当初ブロック処理を想定して +4096 の余裕を持たせていたが、
    現在の実装では不要と判明したため削除。無駄なメモリ確保を解消) */
#define RV_HT_STATE (HT_N + 1)

/* ================= 受信ハンドル ================= */
/* rade_v2_rx が要求する RADE_COMP と、ここで使う COMP は同型(float re/im)。
   ヒルベルト出力・IQバッファは COMP で持ち、rade_v2_rx へは RADE_COMP* として渡す
   (呼び出し時にキャストする。メモリレイアウトが一致しているため安全)。 */

/* IQ 中継バッファの容量(複素サンプル数)。VLA 回避のため固定長。
   1回の push_modem で来る n8 の想定上限 + nin_io(sym_len前後) に
   十分な安全マージンを持たせる。 */
#define RV_IQBUF_CAP 4096

/* FARGAN の1フレーム分 features 数(NB_FEATURES)。rade_v2 の feature_dim(21)
   から先頭 NB_FEATURES(20) だけを渡す。frames_per_step=4 個ぶんが一度に出る。 */
#define RV_FEATURE_DIM      21   /* rade_v2 の1フレームあたり要素数 */
#define RV_FRAMES_PER_STEP  4    /* rade_v2_rx が一度に出すフレーム数 */

/* V1(DR-NOPY)用の固定長バッファ上限。実際の値は create 時に
   rade_n_features_in_out() / rade_n_eoo_bits() で取得し、この上限を
   超えていたら生成失敗にする(VLA回避のための安全な固定長確保)。 */
#define RV_V1_FEATURES_CAP 512
#define RV_V1_EOO_CAP      256

typedef struct {
    int   is_v2;
    void* rade;                 /* V2: RADEV2Context* (V1は次段階) */
    int   n_features_out;       /* rade_v2_n_features_out(ctx) をcreate時に
                                    キャッシュ(2026-07-13: ハードコード84を
                                    廃止し、モデル変更に追従できるようにした) */
    FARGANState fargan;
    int   fargan_primed;        /* priming(先頭5フレーム)済みかのカウンタ管理用 */
    int   fargan_prime_count;   /* priming用に貯めたフレーム数(0..5) */
    float fargan_prime_feats[5 * NB_FEATURES];  /* priming用バッファ(5フレーム分) */

    float gain;                 /* rv_rx_set_gain */

    /* --- [1] ヒルベルト変換の状態(ch.c と同じ構造) --- */
    float htbuf[RV_HT_STATE];   /* 前半 HT_N が過去履歴 */

    /* --- [1]->[2] 橋渡し: ヒルベルト出力(IQ)の中継バッファ ---
       線形バッファ。write_pos までが有効データ末尾、read_pos までが消費済み。
       有効データは buf[read_pos .. write_pos)。 */
    COMP  iqbuf[RV_IQBUF_CAP];
    int   iq_write_pos;
    int   iq_read_pos;
    int   nin_next;             /* rade_v2_rx が次回要求するサンプル数(nin_io) */

    /* 状態キャッシュ(rade_v2_rx の出力を保持して getter で返す) */
    int   last_synced;          /* sig_det をキャッシュ */
    int   last_snr_db;          /* V2 は手段なし → 0 */
    float last_freq_offset;     /* V2 は手段なし → 0 */
    int   callsign_seq;         /* V2 は EOO 未統合 → 0(次段階) */

    /* idle->sync 遷移検知用(2026-07-13追加)。rade_v2_rx の戻り値
       (1=sync/0=idle, rsync_process_symbol の RRX_STATE_SYNC 判定結果)を
       前回値と比較し、idle→sync に変わった瞬間に FARGAN priming をやり直す。
       (7/11 実チャネル試験で sync が5回切れて再取得した実績があり、
        再synchronize直後の音質を優先する方針で再priming を選択した) */
    int   prev_rx_state;

    /* 診断カウンタ(2026-07-14追加。RadeCallTest実運用で「たまに音が出る」
       症状の原因調査用。OutputDebugStringA経由でDebugViewから観察する)。 */
    long  dbg_push_calls;       /* rv_rx_push_modem 呼び出し回数 */
    long  dbg_iq_overflow;      /* IQバッファ溢れでサンプルを捨てた回数 */
    long  dbg_rade_rx_calls;    /* rade_rx()/rade_v2_rx() 実呼び出し回数 */
    int   dbg_last_nin;         /* 直近の nin 値(V1: rade_nin、V2: nin_next) */
    int   dbg_expected_n8;      /* 初回のn8を記憶し、以後の異常検知の基準にする */
    int   dbg_prev_logged_synced;  /* 前回ログ時のsynced値(変化検知用) */

    /* ==== V1(DR-NOPY, rade_api.h)専用フィールド(2026-07-13追加) ====
       ★注意: V1のfeatures内訳(1回のrade_rx呼び出しで何フレーム分出るか)は
       実ソース(rade_dec.c等)を見ずに rade_n_features_in_out() の値から
       「NB_TOTAL_FEATURES(36)の倍数のはず」と推測して組んでいる。
       create時に割り切れるか検証し、割り切れなければ生成失敗にする
       (壊れた前提で音を出すより安全に失敗させる方針)。 */
    struct rade *radeV1;          /* rade_open() の戻り値 */
    int   v1_n_features;          /* rade_n_features_in_out(r) の値 */
    int   v1_n_frames;            /* v1_n_features / NB_TOTAL_FEATURES */
    int   v1_n_eoo_bits;          /* rade_n_eoo_bits(r) の値 */
    float v1_features_buf[RV_V1_FEATURES_CAP];  /* features_out 受け皿 */
    float v1_eoo_buf[RV_V1_EOO_CAP];             /* eoo_out 受け皿 */
    char  v1_callsign_buf[RADE_EOO_CALLSIGN_MAX + 1];  /* 復号済みコールサイン */
} rv_rx_t;

/* ================= グローバル ================= */
/* V1(rade_api.h)の rade_initialize()/rade_finalize() は
   「他のRADE関数より前に一度だけ」呼ぶ、プロセス全体のグローバル初期化。
   ハンドル(rv_rx_create等)ごとに呼ぶと二重初期化になるため、ここ(アプリ
   起動時に1回呼ばれる想定のrv_global_init)に置く(2026-07-13、V1対応時に
   設計修正)。static変数で多重呼び出しを防御しておく。 */
static int g_v1_global_inited = 0;

RV_API void rv_global_init(void)
{
    if (!g_v1_global_inited) {
        rade_initialize();
        g_v1_global_inited = 1;
    }
}
RV_API void rv_global_shutdown(void)
{
    if (g_v1_global_inited) {
        rade_finalize();
        g_v1_global_inited = 0;
    }
}

/* ================= ライフサイクル ================= */
RV_API void* rv_rx_create(int v2)
{
    rv_rx_t* s = (rv_rx_t*)calloc(1, sizeof(rv_rx_t));
    if (!s) return NULL;
    s->is_v2 = v2;
    s->gain  = 1.0f;
    s->fargan_primed = 0;

    if (v2) {
        /* ---- V2 ---- */
        s->rade = (void*)rade_v2_open();
        if (!s->rade) { free(s); return NULL; }

        s->nin_next = rade_v2_sym_len((RADEV2Context*)s->rade);
        s->n_features_out = rade_v2_n_features_out((RADEV2Context*)s->rade);
        if (s->n_features_out != RV_FEATURE_DIM * RV_FRAMES_PER_STEP) {
            rv_dbg(
                "rade_v2_voice: 警告 n_features_out=%d が想定(%d=%d*%d)と不一致。"
                "featuresバッファサイズの前提が崩れている可能性あり。\n",
                s->n_features_out, RV_FEATURE_DIM * RV_FRAMES_PER_STEP,
                RV_FEATURE_DIM, RV_FRAMES_PER_STEP);
        }
    } else {
        /* ---- V1(DR-NOPY, rade_api.h) 2026-07-13追加 ----
           rade_initialize() はここでは呼ばない(rv_global_init でプロセス
           全体1回のみ呼ぶ設計にした)。RadeCallTest 側が起動時に
           rv_global_init() を呼ぶ前提(既存の rade_voice.dll と同じ契約)。
           model_file 引数は DR-NOPY では重みが埋め込みのため無視される
           はず(V2の rade_v2_open(void) が引数無しなのと同じ理由の推測。
           未検証)。 */
        s->radeV1 = rade_open("", RADE_USE_C_DECODER | RADE_VERBOSE_0);
        if (!s->radeV1) { free(s); return NULL; }

        s->v1_n_features = rade_n_features_in_out(s->radeV1);
        s->v1_n_eoo_bits = rade_n_eoo_bits(s->radeV1);

        /* ★未検証の前提: V1の1フレームあたり幅は universal LPCNet 規約の
           NB_TOTAL_FEATURES(36)のはず(lpcnet_demo.c を V1/V2 共通で使う
           ことから推測)。割り切れなければ前提が崩れているので、壊れた
           まま進めず生成失敗にする(安全側に倒す)。 */
        if (s->v1_n_features <= 0 || s->v1_n_features % NB_TOTAL_FEATURES != 0 ||
            s->v1_n_features > RV_V1_FEATURES_CAP ||
            s->v1_n_eoo_bits < 0 || s->v1_n_eoo_bits > RV_V1_EOO_CAP) {
            rv_dbg(
                "rade_v2_voice: V1 features/eoo のサイズ前提が崩れています"
                "(n_features=%d, n_eoo_bits=%d)。実装の見直しが必要です。\n",
                s->v1_n_features, s->v1_n_eoo_bits);
            rade_close(s->radeV1);
            free(s);
            return NULL;
        }
        s->v1_n_frames = s->v1_n_features / NB_TOTAL_FEATURES;
    }

    fargan_init(&s->fargan);   /* load_model 不要(opus.lib 埋め込み)。V1/V2共通 */

    /* ヒルベルト状態ゼロ初期化(先頭 HT_N 履歴)。V1/V2共通 */
    memset(s->htbuf, 0, sizeof(s->htbuf));

    /* IQ中継バッファ初期化。V1/V2共通 */
    s->iq_write_pos = 0;
    s->iq_read_pos  = 0;

    /* V1の初回nin。rade_nin()は毎回問い合わせる方式なので、ここでは
       V2のような初期値キャッシュは不要(push_modem内で都度取得する)。 */

    s->fargan_prime_count = 0;
    s->prev_rx_state = 0;   /* idle から開始 */

    return (void*)s;
}

RV_API void rv_rx_destroy(void* h)
{
    rv_rx_t* s = (rv_rx_t*)h;
    if (!s) return;
    if (s->is_v2) {
        if (s->rade) rade_v2_close((RADEV2Context*)s->rade);
    } else {
        if (s->radeV1) rade_close(s->radeV1);
    }
    free(s);
}

RV_API void rv_rx_set_gain(void* h, float gain)
{
    rv_rx_t* s = (rv_rx_t*)h;
    if (s) s->gain = gain;
}

/* ================= 状態取得 ================= */
RV_API int rv_rx_synced(void* h)
{
    rv_rx_t* s = (rv_rx_t*)h;
    return s ? s->last_synced : 0;
}
RV_API int rv_rx_snr_db(void* h)
{
    /* V2 は rade_v2_rx から SNR を得る手段が無く 0 のまま(last_snr_db初期値)。
       V1 は push_modem 内で rade_snrdB_3k_est() を毎回キャッシュしているので
       ここでは両者とも統一的にキャッシュ値を返すだけでよい
       (2026-07-13: V1対応でキャッシュ経由に統一)。 */
    rv_rx_t* s = (rv_rx_t*)h;
    return s ? s->last_snr_db : 0;
}
RV_API float rv_rx_freq_offset(void* h)
{
    /* V2 は手段が無く 0 のまま。V1 は push_modem 内でキャッシュ済み。 */
    rv_rx_t* s = (rv_rx_t*)h;
    return s ? s->last_freq_offset : 0.0f;
}

/* ================= コールサイン ================= */
RV_API int rv_rx_get_callsign(void* h, unsigned char* dst, int max)
{
    /* V1: rade_rx_get_eoo_callsign() で復号済みの文字列を push_modem 内で
       s->v1_callsign_buf にキャッシュ済み(下記構造体拡張参照)。
       V2: EOOにテキストチャネルが無い(krl_eoo等の新規実装が必要、未着手)
       ため常に空文字。 */
    rv_rx_t* s = (rv_rx_t*)h;
    if (dst && max > 0) dst[0] = 0;
    if (!s || s->is_v2) return 0;
    if (s->v1_callsign_buf[0] == 0) return 0;
    {
        int n = 0;
        while (s->v1_callsign_buf[n] && n < max - 1) { dst[n] = (unsigned char)s->v1_callsign_buf[n]; n++; }
        dst[n] = 0;
        return n;
    }
}
RV_API int rv_rx_callsign_seq(void* h)
{
    rv_rx_t* s = (rv_rx_t*)h;
    return s ? s->callsign_seq : 0;
}
RV_API int rv_rx_protocol(void* h)
{
    /* 0=V1, 1=V2 を返す想定(RadeCallTest 側の解釈に合わせて後で調整) */
    rv_rx_t* s = (rv_rx_t*)h;
    return s ? (s->is_v2 ? 1 : 0) : -1;
}

/* =====================================================================
   [1] ヒルベルト変換: 実信号 n サンプルを s->htbuf の状態で畳み込み、
       複素IQ を s->iqbuf の write_pos 以降へ追記する。
       ch.c 317-346行(David Rowe)を、ブロック単位を可変(n)にして移植。
   ===================================================================== */
static void rv_hilbert_push(rv_rx_t* s, const float* real_in, int n)
{
    int i, k, j;
    for (i = 0; i < n; i++) {
        float re = 0.0f, im = 0.0f;

        /* 履歴末尾(HT_N番目)に新サンプルを置く。
           rv_rx_set_gain で設定されたゲインをここで適用する
           (2026-07-13: 従来 s->gain が保持されるだけで未適用だったバグを修正)。 */
        s->htbuf[HT_N] = real_in[i] * s->gain;

        for (k = 0; k < HT_N; k++) {
            re += s->htbuf[HT_N - k] * ht_coeff[k].real;
            im += s->htbuf[HT_N - k] * ht_coeff[k].imag;
        }

        if (s->iq_write_pos < RV_IQBUF_CAP) {
            s->iqbuf[s->iq_write_pos].real = re;
            s->iqbuf[s->iq_write_pos].imag = im;
            s->iq_write_pos++;
        } else {
            s->dbg_iq_overflow++;   /* 2026-07-14診断: 溢れて捨てた回数 */
        }

        /* 履歴シフト: htbuf[0..HT_N) を1つ前に詰め、末尾を空ける */
        for (j = 0; j < HT_N; j++)
            s->htbuf[j] = s->htbuf[j + 1];
    }
}

/* =====================================================================
   IQ 中継バッファの前詰め: read_pos より前(消費済み)を捨てて巻き戻す。
   ===================================================================== */
static void rv_iqbuf_compact(rv_rx_t* s)
{
    int remain = s->iq_write_pos - s->iq_read_pos;
    if (s->iq_read_pos == 0) return;
    if (remain > 0)
        memmove(s->iqbuf, &s->iqbuf[s->iq_read_pos], remain * sizeof(COMP));
    s->iq_write_pos = remain;
    s->iq_read_pos  = 0;
}

/* =====================================================================
   [3] FARGAN 音声化: features(21要素/フレーム)から先頭 NB_FEATURES(20)を
       使って合成。最初の5フレームは priming(fargan_cont)に使い音声は出さない
       (test_fargan.c / lpcnet_demo と同じ作法)。
       戻り値: 生成した音声サンプル数(0 または FARGAN_FRAME_SIZE)。
   ===================================================================== */
static int rv_fargan_feed(rv_rx_t* s, const float* feat21, float* out)
{
    float feat20[NB_FEATURES];
    OPUS_COPY(feat20, feat21, NB_FEATURES);

    if (s->fargan_prime_count < 5) {
        OPUS_COPY(&s->fargan_prime_feats[s->fargan_prime_count * NB_FEATURES],
                  feat20, NB_FEATURES);
        s->fargan_prime_count++;
        if (s->fargan_prime_count == 5) {
            float zeros[FARGAN_CONT_SAMPLES] = {0};
            fargan_cont(&s->fargan, zeros, s->fargan_prime_feats);
            s->fargan_primed = 1;
        }
        return 0;
    }

    fargan_synthesize(&s->fargan, out, feat20);
    return FARGAN_FRAME_SIZE;
}

/* ================= 核心: rv_rx_push_modem ================= */
RV_API int rv_rx_push_modem(void* h, float* modem8k, int n8,
                            float* speech16kOut, int maxOut)
{
    rv_rx_t* s = (rv_rx_t*)h;
    int out_written = 0;
    int f;

    if (!s || !modem8k || n8 <= 0) return 0;

    /* 2026-07-14診断(第2版): 毎コールバック記録する。
       n8(このコールバックで来たサンプル数)が通常値(約800)から外れて
       いれば、ライブキャプチャ側のグリッチ(オーディオの途切れ・詰まり)の
       直接の証拠になる。通常時は間引いて出し、異常値(n8が想定と大きく
       違う、または前回よりsyncedが変化した)のときだけ即ログすることで
       DebugViewのログ量を抑えつつ、肝心な瞬間を逃さないようにする。 */
    s->dbg_push_calls++;
    {
        int synced_now = s->last_synced;
        int synced_changed = (synced_now != s->dbg_prev_logged_synced);
        /* n8 の想定値(初回に記憶。以後これと違えばグリッチとみなす) */
        if (s->dbg_expected_n8 == 0) s->dbg_expected_n8 = n8;
        int n8_anomaly = (n8 < s->dbg_expected_n8 * 3 / 4 ||
                          n8 > s->dbg_expected_n8 * 5 / 4);

        if (synced_changed || n8_anomaly || (s->dbg_push_calls % 10 == 1)) {
            rv_dbg("[rv_rx %s] call#%ld n8=%d(exp%d)%s iqbuf=%d/%d overflow=%ld rade_rx_calls=%ld last_nin=%d synced=%d%s\n",
                   s->is_v2 ? "V2" : "V1", s->dbg_push_calls, n8, s->dbg_expected_n8,
                   n8_anomaly ? " !ANOMALY!" : "",
                   s->iq_write_pos - s->iq_read_pos, RV_IQBUF_CAP,
                   s->dbg_iq_overflow, s->dbg_rade_rx_calls, s->dbg_last_nin,
                   synced_now, synced_changed ? " !CHANGED!" : "");
            s->dbg_prev_logged_synced = synced_now;
        }
    }

    /* --- [1] 実信号 → 複素IQ(状態は s->htbuf に保持)。V1/V2共通 --- */
    rv_hilbert_push(s, modem8k, n8);

    if (s->is_v2) {
        /* ---- V2 ---- */
        float features[RV_FEATURE_DIM * RV_FRAMES_PER_STEP];
        int has_features = 0, sig_det = 0, sine_det = 0;

        while ((s->iq_write_pos - s->iq_read_pos) >= s->nin_next) {
            int nin_before = s->nin_next;
            int state;

            state = rade_v2_rx((RADEV2Context*)s->rade,
                               (const RADE_COMP*)&s->iqbuf[s->iq_read_pos],
                               &s->nin_next,
                               features, &has_features, &sig_det, &sine_det);
            s->dbg_rade_rx_calls++;
            s->dbg_last_nin = nin_before;

            s->iq_read_pos += nin_before;
            s->last_synced = sig_det;
            (void)sine_det;

            /* idle->sync 遷移を検知したら FARGAN priming をやり直す
               (2026-07-13追加。設計判断: 再同期時は音質優先で再priming する)。 */
            if (state == 1 && s->prev_rx_state == 0) {
                s->fargan_prime_count = 0;
                s->fargan_primed = 0;
            }
            s->prev_rx_state = state;

            if (has_features) {
                for (f = 0; f < RV_FRAMES_PER_STEP; f++) {
                    float pcm[FARGAN_FRAME_SIZE];
                    int n = rv_fargan_feed(s, &features[f * RV_FEATURE_DIM], pcm);
                    if (n > 0 && out_written + n <= maxOut) {
                        memcpy(&speech16kOut[out_written], pcm, n * sizeof(float));
                        out_written += n;
                    }
                }
            }
        }
    } else {
        /* ---- V1(DR-NOPY) 2026-07-13追加 ----
           呼び出し規約がV2と違う: rade_nin()で「次回必要なサンプル数」を
           毎回問い合わせてから rade_rx() を呼ぶ(V2のnin_ioポインタ渡しとは
           別方式)。features幅は v1_n_frames*NB_TOTAL_FEATURES(推測、create
           時に検証済み)。 */
        while (1) {
            int nin = rade_nin(s->radeV1);
            int n_out, has_eoo = 0, state;

            if ((s->iq_write_pos - s->iq_read_pos) < nin) break;

            n_out = rade_rx(s->radeV1, s->v1_features_buf, &has_eoo,
                            s->v1_eoo_buf,
                            (RADE_COMP*)&s->iqbuf[s->iq_read_pos]);
            s->dbg_rade_rx_calls++;
            s->dbg_last_nin = nin;
            s->iq_read_pos += nin;

            state = rade_sync(s->radeV1);
            s->last_synced      = state;
            s->last_snr_db      = rade_snrdB_3k_est(s->radeV1);
            s->last_freq_offset = rade_freq_offset(s->radeV1);

            if (state == 1 && s->prev_rx_state == 0) {
                s->fargan_prime_count = 0;
                s->fargan_primed = 0;
            }
            s->prev_rx_state = state;

            if (n_out > 0) {
                /* v1_features_buf は v1_n_frames * NB_TOTAL_FEATURES(36)幅。
                   各フレームの先頭NB_FEATURES(20)だけFARGANへ(V2と同じ
                   関数を再利用。ストライドが21ではなく36な点だけ違う)。 */
                for (f = 0; f < s->v1_n_frames; f++) {
                    float pcm[FARGAN_FRAME_SIZE];
                    int n = rv_fargan_feed(s, &s->v1_features_buf[f * NB_TOTAL_FEATURES], pcm);
                    if (n > 0 && out_written + n <= maxOut) {
                        memcpy(&speech16kOut[out_written], pcm, n * sizeof(float));
                        out_written += n;
                    }
                }
            }

            if (has_eoo) {
                /* 2026-07-14: バッファ拡張(9->64バイト)だけではクラッシュが
                   直らなかった。callsign用バッファ以外(呼び出しそのもの、
                   あるいは v1_eoo_buf 自体)が壊れている疑いが強まったため、
                   まず無条件ログで実測する。call回数に関わらず毎回出す
                   (クラッシュ直前の最後の1行が手がかりになる)。 */
                char cs[64];
                int n;
                rv_dbg("[rv_rx V1] EOO発生: n_out=%d v1_n_eoo_bits=%d call#%ld\n",
                       n_out, s->v1_n_eoo_bits, s->dbg_push_calls);
                memset(cs, 0, sizeof(cs));
                n = rade_rx_get_eoo_callsign(s->v1_eoo_buf, s->v1_n_eoo_bits, cs);
                rv_dbg("[rv_rx V1] EOOコールサイン戻り値: n=%d\n", n);
                if (n > 0 && n <= RADE_EOO_CALLSIGN_MAX) {
                    memcpy(s->v1_callsign_buf, cs, RADE_EOO_CALLSIGN_MAX);
                    s->v1_callsign_buf[RADE_EOO_CALLSIGN_MAX] = 0;
                    s->callsign_seq++;
                }
                rv_dbg("[rv_rx V1] EOO処理完了\n");
            }
        }
    }

    rv_iqbuf_compact(s);

    return out_written;
}

/* =====================================================================
   ================= TX 側(2026-07-13 追加) =============================
   speech16kOut(音声) → LPCNet特徴抽出 → rade_v2_tx → 実信号8kHzモデム
   RXとは逆方向。RXのヒルベルト/リングバッファ/FARGAN primingに相当する
   複雑な状態管理は無く、比較的単純な直線的パイプラインになる。

   処理単位: LPCNet特徴抽出は LPCNET_FRAME_SIZE(160サンプル@16kHz=10ms)ごと。
   RADE V2 は frames_per_step(4フレーム=40ms)単位で rade_v2_tx を呼ぶため、
   4サブフレームぶんの特徴量(21要素×4=84)を貯めてから1回 rade_v2_tx を呼ぶ。
   出力は複素IQ(Ns*sym_len=320サンプル)。実信号化は「実部を取り出す」
   (7/11 iq_to_wav.py で実証済みの手法)。
   ===================================================================== */

#define RV_TXBUF_CAP 4096   /* speech16k 中継バッファ容量(サンプル数) */

/* auxdata(feature[20])は tx2.py と同じく固定 -1(7/13時点、EOOテキスト
   チャネルは未実装のため常に定数)。7/9調査で確定済みの仕様。 */
#define RV_AUXDATA_FIXED (-1.0f)

typedef struct {
    int   is_v2;
    void* rade;                 /* V2: RADEV2Context* */
    struct rade *radeV1;         /* V1: rade_open() の戻り値(2026-07-13追加) */

    LPCNetEncState *enc;         /* LPCNet特徴抽出器(opus内蔵、BSD)。V1/V2共通 */
    int   arch;                  /* opus_select_arch() をcreate時にキャッシュ */

    int   n_tx_out;              /* V2: rade_v2_n_tx_out (320想定) */
    int   n_features_in;         /* V2: rade_v2_n_features_in (84想定) */
    int   n_eoo_out;              /* V2: rade_v2_n_eoo_out */

    /* V1用(2026-07-13追加)。RXと同様、features幅は実行時に検証する
       (NB_TOTAL_FEATURES=36の倍数のはず、という推測に基づく防御的実装)。 */
    int   v1_n_features;          /* rade_n_features_in_out(r) */
    int   v1_n_frames;            /* v1_n_features / NB_TOTAL_FEATURES */
    int   v1_n_tx_out;            /* rade_n_tx_out(r) */
    int   v1_n_eoo_out;           /* rade_n_tx_eoo_out(r) */
    float v1_features_accum[RV_V1_FEATURES_CAP];

    float gain;                  /* rv_tx_set_gain */

    /* speech16k 中継バッファ(RXのiqbufと同じ線形バッファ+前詰め方式) */
    float speech_buf[RV_TXBUF_CAP];
    int   speech_write_pos;
    int   speech_read_pos;

    /* V2: 4サブフレーム分の features を貯める(84要素、21刻み) */
    float features_accum[RV_FEATURE_DIM * RV_FRAMES_PER_STEP];
    int   subframe_count;        /* 0..4(V2) / 0..v1_n_frames-1(V1) */

    char  callsign[RADE_EOO_CALLSIGN_MAX + 1];
                                  /* V1: rade_tx_set_eoo_callsign() へ即反映
                                     (2026-07-13、本物の実装)。
                                     V2: EOOが固定波形でテキストchannelが
                                     無いため、保存するだけで送信には未反映
                                     (krl_eoo等の新規実装が必要、未着手)。 */
} rv_tx_t;

/* ================= TX ライフサイクル ================= */
RV_API void* rv_tx_create(int v2)
{
    rv_tx_t* s = (rv_tx_t*)calloc(1, sizeof(rv_tx_t));
    if (!s) return NULL;
    s->is_v2 = v2;
    s->gain  = 1.0f;

    if (v2) {
        /* ---- V2 ---- */
        s->rade = (void*)rade_v2_open();
        if (!s->rade) { free(s); return NULL; }

        s->n_tx_out      = rade_v2_n_tx_out((RADEV2Context*)s->rade);
        s->n_features_in = rade_v2_n_features_in((RADEV2Context*)s->rade);
        s->n_eoo_out     = rade_v2_n_eoo_out((RADEV2Context*)s->rade);

        if (s->n_features_in != RV_FEATURE_DIM * RV_FRAMES_PER_STEP) {
            rv_dbg(
                "rade_v2_voice(tx): 警告 n_features_in=%d が想定(%d)と不一致。\n",
                s->n_features_in, RV_FEATURE_DIM * RV_FRAMES_PER_STEP);
        }
        {
            int expected_tx_out = 2 * rade_v2_sym_len((RADEV2Context*)s->rade);
            if (s->n_tx_out != expected_tx_out) {
                rv_dbg(
                    "rade_v2_voice(tx): 警告 n_tx_out=%d が想定(%d=2*sym_len)と不一致。\n",
                    s->n_tx_out, expected_tx_out);
            }
        }
    } else {
        /* ---- V1(DR-NOPY) 2026-07-13追加 ----
           rade_initialize() はここでは呼ばない(rv_global_initで一度だけ)。 */
        s->radeV1 = rade_open("", RADE_USE_C_ENCODER | RADE_VERBOSE_0);
        if (!s->radeV1) { free(s); return NULL; }

        s->v1_n_features = rade_n_features_in_out(s->radeV1);
        s->v1_n_tx_out   = rade_n_tx_out(s->radeV1);
        s->v1_n_eoo_out  = rade_n_tx_eoo_out(s->radeV1);

        if (s->v1_n_features <= 0 || s->v1_n_features % NB_TOTAL_FEATURES != 0 ||
            s->v1_n_features > RV_V1_FEATURES_CAP ||
            s->v1_n_tx_out <= 0 || s->v1_n_tx_out > 4096 ||
            s->v1_n_eoo_out <= 0 || s->v1_n_eoo_out > 4096) {
            rv_dbg(
                "rade_v2_voice(tx): V1 のサイズ前提が崩れています"
                "(n_features=%d, n_tx_out=%d, n_eoo_out=%d)。\n",
                s->v1_n_features, s->v1_n_tx_out, s->v1_n_eoo_out);
            rade_close(s->radeV1);
            free(s);
            return NULL;
        }
        s->v1_n_frames = s->v1_n_features / NB_TOTAL_FEATURES;
    }

    s->enc = lpcnet_encoder_create();
    if (!s->enc) {
        if (s->is_v2) rade_v2_close((RADEV2Context*)s->rade); else rade_close(s->radeV1);
        free(s); return NULL;
    }
    s->arch = opus_select_arch();

    s->speech_write_pos = 0;
    s->speech_read_pos  = 0;
    s->subframe_count   = 0;
    s->callsign[0]       = 0;

    return (void*)s;
}

RV_API void rv_tx_destroy(void* h)
{
    rv_tx_t* s = (rv_tx_t*)h;
    if (!s) return;
    if (s->enc) lpcnet_encoder_destroy(s->enc);
    if (s->is_v2) {
        if (s->rade) rade_v2_close((RADEV2Context*)s->rade);
    } else {
        if (s->radeV1) rade_close(s->radeV1);
    }
    free(s);
}

RV_API void rv_tx_set_gain(void* h, float gain)
{
    rv_tx_t* s = (rv_tx_t*)h;
    if (s) s->gain = gain;
}

RV_API void rv_tx_set_callsign(void* h, const char* callsign)
{
    rv_tx_t* s = (rv_tx_t*)h;
    if (!s) return;
    if (callsign) {
        strncpy(s->callsign, callsign, sizeof(s->callsign) - 1);
        s->callsign[sizeof(s->callsign) - 1] = 0;
    } else {
        s->callsign[0] = 0;
    }
    if (!s->is_v2 && s->radeV1) {
        /* V1: rade_api.h 標準の簡易コールサインチャネル(raw 7bit ASCII,
           LDPC/CRC無し)へ即反映。次回 rv_tx_eoo() 呼び出し時にこの内容が
           送信される。(2026-07-13: 本物の実装。公式FreeDV互換のkrl_eoo方式
           とは別物である点に注意 ── これはRadeCallTest独自実装同士の
           通信を想定した簡易版) */
        rade_tx_set_eoo_callsign(s->radeV1, s->callsign);
    }
    /* V2: EOOが固定波形でテキストchannelが無いため、保存のみで未反映
       (krl_eoo等の新規実装が必要、未着手)。 */
}

RV_API int rv_tx_protocol(void* h)
{
    rv_tx_t* s = (rv_tx_t*)h;
    return s ? (s->is_v2 ? 1 : 0) : -1;
}

/* ================= 核心: rv_tx_push_speech ================= */
RV_API int rv_tx_push_speech(void* h, float* speech16k, int n16,
                             float* modem8kOut, int maxOut)
{
    rv_tx_t* s = (rv_tx_t*)h;
    int out_written = 0;
    int i, remain;
    int frames_per_step = s ? (s->is_v2 ? RV_FRAMES_PER_STEP : s->v1_n_frames) : 0;

    if (!s || !speech16k || n16 <= 0) return 0;

    /* --- 入力を中継バッファへ追記(gain適用)。V1/V2共通 --- */
    for (i = 0; i < n16; i++) {
        if (s->speech_write_pos < RV_TXBUF_CAP) {
            s->speech_buf[s->speech_write_pos++] = speech16k[i] * s->gain;
        }
    }

    /* --- LPCNET_FRAME_SIZE(160)単位で特徴抽出 → frames_per_step貯めて送信 --- */
    while ((s->speech_write_pos - s->speech_read_pos) >= LPCNET_FRAME_SIZE) {
        opus_int16 pcm16[LPCNET_FRAME_SIZE];
        float feat36[NB_TOTAL_FEATURES];
        int k;

        for (k = 0; k < LPCNET_FRAME_SIZE; k++) {
            float v = s->speech_buf[s->speech_read_pos + k];
            pcm16[k] = (opus_int16)floor(0.5 + (v > 1.0f ? 32767.0f :
                        (v < -1.0f ? -32768.0f : v * 32768.0f)));
        }
        s->speech_read_pos += LPCNET_FRAME_SIZE;

        lpcnet_compute_single_frame_features(s->enc, pcm16, feat36, s->arch);

        if (s->is_v2) {
            /* 21要素/フレーム = 先頭NB_FEATURES(20) + auxdata固定(-1) */
            float *dst21 = &s->features_accum[s->subframe_count * RV_FEATURE_DIM];
            memcpy(dst21, feat36, NB_FEATURES * sizeof(float));
            dst21[NB_FEATURES] = RV_AUXDATA_FIXED;
        } else {
            /* V1: auxdata概念が無いはずなので、36要素そのままコピー
               (★未検証の前提。V1のcore_encoder入力レイアウトが本当に
               「LPCNet特徴量そのまま」かは実ソース未確認)。 */
            memcpy(&s->v1_features_accum[s->subframe_count * NB_TOTAL_FEATURES],
                   feat36, NB_TOTAL_FEATURES * sizeof(float));
        }

        s->subframe_count++;

        if (s->subframe_count == frames_per_step) {
            RADE_COMP tx_iq[4096];
            int n_out = s->is_v2 ? s->n_tx_out : s->v1_n_tx_out;
            int j;

            if (n_out > 0 && n_out <= 4096) {
                if (s->is_v2)
                    rade_v2_tx((RADEV2Context*)s->rade, s->features_accum, tx_iq);
                else
                    rade_tx(s->radeV1, tx_iq, s->v1_features_accum);

                /* 複素IQ → 実信号: 実部を取り出す(V1/V2共通の手法) */
                for (j = 0; j < n_out; j++) {
                    if (out_written < maxOut) {
                        modem8kOut[out_written++] = ((COMP*)tx_iq)[j].real;
                    }
                }
            }
            s->subframe_count = 0;
        }
    }

    /* --- 中継バッファの前詰め --- */
    remain = s->speech_write_pos - s->speech_read_pos;
    if (s->speech_read_pos > 0) {
        if (remain > 0)
            memmove(s->speech_buf, &s->speech_buf[s->speech_read_pos],
                    remain * sizeof(float));
        s->speech_write_pos = remain;
        s->speech_read_pos  = 0;
    }

    return out_written;
}

/* ================= TX EOO ================= */
RV_API int rv_tx_eoo(void* h, float* modem8kOut, int maxOut)
{
    rv_tx_t* s = (rv_tx_t*)h;
    RADE_COMP eoo_iq[4096];
    int n_out, j, written = 0;

    if (!s) return 0;
    n_out = s->is_v2 ? s->n_eoo_out : s->v1_n_eoo_out;
    if (n_out <= 0 || n_out > 4096) return 0;

    if (s->is_v2)
        rade_v2_tx_eoo((RADEV2Context*)s->rade, eoo_iq);
    else
        rade_tx_eoo(s->radeV1, eoo_iq);   /* V1: rv_tx_set_callsign 済みなら
                                             ここでコールサインが載る */

    for (j = 0; j < n_out; j++) {
        if (written < maxOut) {
            modem8kOut[written++] = ((COMP*)eoo_iq)[j].real;
        }
    }
    return written;
}
