/*---------------------------------------------------------------------------*\

  test_frame_sync.c

  FrameSyncNet C 実装 (rade_frame_sync.c) が Python 版 models_sync.py と
  数値的に一致することを検証する単体テスト。

  入力:  fsync_in.f32   ... float[input_dim]           (Python が書き出す既知入力)
  基準:  fsync_ref.f32  ... float[1]                   (Python の出力メトリック)
  出力:  標準出力に C の出力と相対誤差を表示。合格なら exit 0。

  重み: rade_sync_data.c 内の radesync_arrays[] を使用。
        (export_rade_v2_weights.py が生成)

  使い方:
    1. Python 側で gen_fsync_ref.py を実行し fsync_in.f32 / fsync_ref.f32 を作る
    2. 本プログラムをビルドして実行

\*---------------------------------------------------------------------------*/

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "rade_frame_sync.h"

/* export_rade_v2_weights.py が rade_sync_data.c に生成する重み配列 */
extern const WeightArray radesync_arrays[];

#define LATENT_DIM 56   /* V2 */
#define TOL        1e-4f

static int read_f32(const char *path, float *buf, int n)
{
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "ファイルを開けません: %s\n", path); return -1; }
    size_t got = fread(buf, sizeof(float), (size_t)n, f);
    fclose(f);
    if ((int)got != n) {
        fprintf(stderr, "%s: 要素数不足 (期待 %d, 実際 %zu)\n", path, n, got);
        return -1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    const char *in_path  = (argc > 1) ? argv[1] : "fsync_in.f32";
    const char *ref_path = (argc > 2) ? argv[2] : "fsync_ref.f32";

    float az_hat[LATENT_DIM];
    float ref;

    if (read_f32(in_path,  az_hat, LATENT_DIM)) return 1;
    if (read_f32(ref_path, &ref,   1))          return 1;

    FrameSyncNet net;
    int rc = fsync_init(&net, radesync_arrays, LATENT_DIM);
    if (rc != 0) {
        fprintf(stderr, "fsync_init 失敗 (層 %d の重みが radesync_arrays に無い)\n", rc);
        return 1;
    }

    float y = fsync_forward(&net, az_hat, 0 /*arch*/);

    float abs_err = fabsf(y - ref);
    float rel_err = abs_err / (fabsf(ref) + 1e-12f);

    printf("C出力      = %.8f\n", y);
    printf("Python基準 = %.8f\n", ref);
    printf("絶対誤差   = %.3e\n", abs_err);
    printf("相対誤差   = %.3e\n", rel_err);

    if (rel_err < TOL || abs_err < TOL) {
        printf("=== 合格 (誤差 < %.0e) ===\n", TOL);
        return 0;
    } else {
        printf("=== 不合格 (誤差 >= %.0e) ===\n", TOL);
        return 2;
    }
}
