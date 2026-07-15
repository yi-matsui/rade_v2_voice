/* rade_v2_core.h ── V2 データ用の最小コアヘッダ
   export 生成の rade_sync_data.h / rade_*_v2_data.h が要求する型を供給する。
   nnet の型を引き込み、生成コードが typedef 無しで使う構造体名を前方 typedef する。 */
#ifndef RADE_V2_CORE_H
#define RADE_V2_CORE_H
#include <stdlib.h>
#include "opus_types.h"
#include "nnet.h"
typedef struct RADESync   RADESync;
typedef struct RADEEncV2  RADEEncV2;
typedef struct RADEDecV2  RADEDecV2;
#endif
