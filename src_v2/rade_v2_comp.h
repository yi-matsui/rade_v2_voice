/*---------------------------------------------------------------------------*\

  rade_v2_comp.h

  RADE V2 C 実装で使う単精度複素数 RADE_COMP と、その基本演算。
  numpy complex64 と同一のメモリ配置(real, imag の順の float2)。

  既存 nopy の rade_api.h / rade_dsp.h が既に RADE_COMP を定義している場合は
  __RADE_COMP__ ガードで二重定義を避ける。演算ヘルパーは rc_ 接頭辞で衝突回避。

\*---------------------------------------------------------------------------*/

#ifndef RADE_V2_COMP_H
#define RADE_V2_COMP_H

#include <math.h>

#ifndef __RADE_COMP__
#define __RADE_COMP__
typedef struct {
  float real;
  float imag;
} RADE_COMP;
#endif

/* 複素演算(すべて値渡し。numpy と同じ規約) */

static inline RADE_COMP rc_make(float re, float im) {
    RADE_COMP z; z.real = re; z.imag = im; return z;
}

static inline RADE_COMP rc_add(RADE_COMP a, RADE_COMP b) {
    return rc_make(a.real + b.real, a.imag + b.imag);
}

static inline RADE_COMP rc_sub(RADE_COMP a, RADE_COMP b) {
    return rc_make(a.real - b.real, a.imag - b.imag);
}

/* a * b */
static inline RADE_COMP rc_mul(RADE_COMP a, RADE_COMP b) {
    return rc_make(a.real * b.real - a.imag * b.imag,
                   a.real * b.imag + a.imag * b.real);
}

/* a * conj(b)  ── 相関でよく使う */
static inline RADE_COMP rc_mul_conj(RADE_COMP a, RADE_COMP b) {
    return rc_make(a.real * b.real + a.imag * b.imag,
                   a.imag * b.real - a.real * b.imag);
}

/* |z|^2 */
static inline float rc_abs2(RADE_COMP z) {
    return z.real * z.real + z.imag * z.imag;
}

/* |z| */
static inline float rc_abs(RADE_COMP z) {
    return sqrtf(rc_abs2(z));
}

/* arg(z) = atan2(imag, real) */
static inline float rc_arg(RADE_COMP z) {
    return atan2f(z.imag, z.real);
}

/* スカラ倍 */
static inline RADE_COMP rc_scale(RADE_COMP z, float s) {
    return rc_make(z.real * s, z.imag * s);
}

/* exp(j*theta) */
static inline RADE_COMP rc_expj(float theta) {
    return rc_make(cosf(theta), sinf(theta));
}

#endif /* RADE_V2_COMP_H */
