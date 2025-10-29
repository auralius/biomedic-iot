import ssl, struct, time, collections
import numpy as np
import paho.mqtt.client as mqtt

# ------- MQTT settings -------
HOST, PORT = "bcf21ddabed1412aaf638a09b4732f50.s1.eu.hivemq.cloud", 8883
USER, PASS = "Device03", "Device03"
TOPIC = "audio/mono"

# ------- Audio format (target playback) -------
FS = 8000                  # Hz (mono)
S_PER_BLOCK = 0.5
SAMPLES_PER_BLOCK = int(FS * S_PER_BLOCK)          # 4000 samples/block @ 8 kHz
BYTES_HEADER = 4                                   # uint32 block_id
BYTES_TOTAL  = BYTES_HEADER + SAMPLES_PER_BLOCK*2  # 4 + 8000 = 8004 bytes

# ------- Realtime audio output (sounddevice) -------
import sounddevice as sd

play_fifo = collections.deque()
fifo_samples = 0
last_block_id = None

def audio_callback(outdata, frames, time_info, status):
    """Fill `outdata` (int16 mono) from FIFO; pad with zeros if empty."""
    global fifo_samples
    if status:
        pass  # underruns/overruns reported here

    needed = frames
    out = np.empty((frames,), dtype=np.int16)
    filled = 0

    while filled < needed:
        if not play_fifo:
            out[filled:needed] = 0
            break
        chunk = play_fifo[0]
        take = min(needed - filled, len(chunk))
        out[filled:filled+take] = chunk[:take]
        if take == len(chunk):
            play_fifo.popleft()
        else:
            play_fifo[0] = chunk[take:]
        filled += take
        fifo_samples -= take

    outdata[:] = out.reshape(-1, 1)

# Create + start the stream (you can set a device via sd.default.device if needed)
stream = sd.OutputStream(samplerate=FS, channels=1, dtype="int16",
                         blocksize=1024, callback=audio_callback)
stream.start()

# ------- MQTT callbacks (v5 + v2 signatures) -------
def on_connect(client, userdata, flags, reason_code, properties):
    print(f"[MQTT] Connected: rc={reason_code}")
    client.subscribe(TOPIC, qos=0)

def on_disconnect(client, userdata, reason_code, properties):
    print(f"[MQTT] Disconnected: rc={reason_code}")

def on_message(client, userdata, msg):
    global last_block_id, fifo_samples
    p = msg.payload
    if len(p) != BYTES_TOTAL:
        # Expect 4-byte block_id + 4000 int16 samples
        # e.g., publisher at 8 kHz mono with 0.5 s blocks
        return

    block_id = int.from_bytes(p[:4], "little", signed=False)
    audio = np.frombuffer(p[4:], dtype="<i2").copy()  # int16 mono (4000 samples)

    # Gap fill by block_id (insert silent 0.5 s blocks if missing)
    if last_block_id is not None:
        gap = (block_id - last_block_id) - 1
        if gap > 0:
            play_fifo.extend([np.zeros(SAMPLES_PER_BLOCK, dtype=np.int16)] * gap)
            fifo_samples += gap * SAMPLES_PER_BLOCK

    play_fifo.append(audio)
    fifo_samples += audio.size
    last_block_id = block_id

# ------- MQTT client -------
client = mqtt.Client(
    client_id="linux-realtime-speaker",
    protocol=mqtt.MQTTv5,
    callback_api_version=mqtt.CallbackAPIVersion.VERSION2
)
client.username_pw_set(USER, PASS)
client.tls_set(tls_version=ssl.PROTOCOL_TLS_CLIENT)
client.tls_insecure_set(False)

client.on_connect = on_connect
client.on_disconnect = on_disconnect
client.on_message = on_message

print("[Main] Connecting ...")
client.connect(HOST, PORT, keepalive=30)

print("[Main] Playing incoming audio live at 8 kHz (no preroll). Ctrl-C to stop.")
try:
    while True:
        client.loop(timeout=0.2)
        time.sleep(0.02)  # keep CPU calm
except KeyboardInterrupt:
    print("\n[Main] Stopping...")
finally:
    try:
        client.disconnect()
    except Exception:
        pass
    try:
        stream.stop()
        stream.close()
    except Exception:
        pass
    print("[Main] Disconnected. Bye.")
