import wave
with open('out.s16','rb') as f:
    data = f.read()
with wave.open('out.wav','wb') as w:
    w.setnchannels(1)
    w.setsampwidth(2)
    w.setframerate(16000)
    w.writeframes(data)
print('out.wav 作成完了:', len(data), 'バイト')