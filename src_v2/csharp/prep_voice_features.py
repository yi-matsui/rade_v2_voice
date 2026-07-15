"""
prep_voice_features.py

lpcnet_demo -features で抽出した生特徴量(36要素/フレーム)を、
RADE V2 encoder が期待する形式(21要素/フレーム = 先頭20 + auxdata(-1))
に変換し、C#のループバックテストで読める単純バイナリ形式で保存する。

さらに、frames_per_step(4)の倍数にフレーム数を切り詰める
(encoderは4フレームまとめて処理するため)。

使い方:
    python prep_voice_features.py features_in.f32 features_v2.f32

入力: lpcnet_demo -features で作った .f32 (36 floats/frame, float32)
出力: features_v2.f32 (21 floats/frame, float32, 4の倍数フレーム数に切り詰め済み)
      これをC#側で読み込み、frames_per_step(4)ずつ束ねてTxへ渡す。
"""
import sys
import numpy as np

NB_TOTAL_FEATURES = 36
NUM_USED_FEATURES = 20
FRAMES_PER_STEP = 4

def main():
    if len(sys.argv) < 3:
        print("usage: python prep_voice_features.py <features_in.f32> <features_v2.f32>", file=sys.stderr)
        sys.exit(1)
    in_path, out_path = sys.argv[1], sys.argv[2]

    raw = np.fromfile(in_path, dtype=np.float32)
    if len(raw) % NB_TOTAL_FEATURES != 0:
        print(f"警告: 入力サイズが{NB_TOTAL_FEATURES}の倍数ではありません。末尾を切り捨てます。", file=sys.stderr)
    n_frames = len(raw) // NB_TOTAL_FEATURES
    raw = raw[:n_frames * NB_TOTAL_FEATURES].reshape(n_frames, NB_TOTAL_FEATURES)

    # frames_per_step(4)の倍数に切り詰め(encoderのstride要件)
    n_rounded = (n_frames // FRAMES_PER_STEP) * FRAMES_PER_STEP
    if n_rounded == 0:
        print("エラー: 音声が短すぎます(最低4フレーム=40ms相当が必要)。", file=sys.stderr)
        sys.exit(1)
    raw = raw[:n_rounded]

    used = raw[:, :NUM_USED_FEATURES]                       # (n_rounded, 20)
    auxdata = -np.ones((n_rounded, 1), dtype=np.float32)     # auxdata=-1(通常送信を示す)
    features_v2 = np.concatenate([used, auxdata], axis=1).astype(np.float32)  # (n_rounded, 21)

    features_v2.flatten().tofile(out_path)

    print(f"入力フレーム数: {n_frames} (10ms/フレーム)")
    print(f"出力フレーム数: {n_rounded} ({n_rounded/100:.2f}秒相当)")
    print(f"modemフレーム数(4フレームずつ): {n_rounded // FRAMES_PER_STEP}")
    print(f"wrote {out_path} ({n_rounded}*21 floats)")

if __name__ == "__main__":
    main()
