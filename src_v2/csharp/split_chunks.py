import numpy as np

IN_PATH = "features_hat36_en_noclamp.f32"
# 1フレーム=10ms相当。10秒 = 1000フレーム分だが、モデルのframes_per_step(4)の
# 倍数に丸めておく方が安全(状態境界を跨がないように)
CHUNK_FRAMES = 1000

f = np.fromfile(IN_PATH, dtype=np.float32).reshape(-1, 36)
n = len(f)
print(f"総フレーム数: {n} ({n*0.01:.1f}秒相当)")

n_chunks = (n + CHUNK_FRAMES - 1) // CHUNK_FRAMES
for i in range(n_chunks):
    st = i * CHUNK_FRAMES
    en = min(st + CHUNK_FRAMES, n)
    chunk = f[st:en]
    out_path = f"features_chunk_{i:02d}.f32"
    chunk.astype(np.float32).tofile(out_path)
    print(f"chunk {i}: frame {st}〜{en} ({len(chunk)}フレーム, {len(chunk)*0.01:.1f}秒) -> {out_path}")

print(f"\n{n_chunks}個のチャンクに分割完了")
