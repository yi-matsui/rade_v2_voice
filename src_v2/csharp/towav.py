import wave
with open('out2.s16','rb') as f:
    data = f.read()
with wave.open('out2.wav','wb') as w:
    w.setnchannels(1); w.setsampwidth(2); w.setframerate(16000)
    w.writeframes(data)
print('out2.wav 作成完了:', len(data), 'バイト')