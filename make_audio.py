import math
import os
import struct
import wave

os.makedirs('data', exist_ok=True)
path = 'data/audio.wav'

# Generate a simple melody: C4, D4, E4, F4, G4, A4, B4, C5
notes = [261.63, 293.66, 329.63, 349.23, 392.00, 440.00, 493.88, 523.25]
sample_rate = 16000
note_duration = 0.3  # seconds per note
total_duration = note_duration * len(notes)
frames = int(sample_rate * total_duration)

with wave.open(path, 'wb') as w:
    w.setnchannels(1)
    w.setsampwidth(2)
    w.setframerate(sample_rate)
    for i in range(frames):
        t = i / sample_rate
        note_idx = int(t / note_duration)
        if note_idx >= len(notes):
            note_idx = len(notes) - 1
        local_t = t - note_idx * note_duration
        freq = notes[note_idx]
        # Apply envelope: fade-in 5ms, fade-out 5ms
        env = 1.0
        fade_len = int(0.005 * sample_rate)
        pos_in_note = i - note_idx * int(note_duration * sample_rate)
        if pos_in_note < fade_len:
            env = pos_in_note / fade_len
        elif pos_in_note > int(note_duration * sample_rate) - fade_len:
            env = (int(note_duration * sample_rate) - pos_in_note) / fade_len
        val = int(12000 * env * math.sin(2 * math.pi * freq * t))
        w.writeframesraw(struct.pack('<h', val))
print(path)