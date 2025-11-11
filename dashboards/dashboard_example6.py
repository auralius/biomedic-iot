#!/usr/bin/env python3
# pip install paho-mqtt matplotlib pillow certifi

import ssl
from io import BytesIO
from collections import deque
import certifi
import matplotlib.pyplot as plt
from PIL import Image
from paho.mqtt import client as mqtt

MQTT_HOST  = "4a48091aa0ac475d93f3f294b735ba3d.s1.eu.hivemq.cloud"
MQTT_PORT  = 8883
MQTT_USER  = "Device02"
MQTT_PASS  = "Device02"
TOPIC_JPEG = "Device01/cam-jpeg"   # payload = raw JPEG bytes (no header)

fig, ax = None, None
sizes = deque(maxlen=100)   # rolling window of last 100 frame sizes (bytes)
frame_count = 0

def kb(n_bytes: int) -> float:
    return n_bytes / 1024.0

def on_connect(client, userdata, flags, rc):
    print(f"[MQTT] Connected (rc={rc})")
    client.subscribe(TOPIC_JPEG, qos=0)
    print(f"[MQTT] Subscribed to {TOPIC_JPEG}")

def on_message(client, userdata, msg):
    global fig, ax, frame_count
    payload = msg.payload
    if not payload:
        return

    sizes.append(len(payload))
    frame_count += 1

    # --- decode WITHOUT forcing grayscale ---
    try:
        img = Image.open(BytesIO(payload))
        # Normalize modes:
        # - L      -> grayscale (2D)
        # - RGB    -> color (HxWx3)
        # - RGBA   -> drop alpha to avoid compositing surprises
        # - P/YCbCr/etc. -> convert to RGB
        if img.mode == "L":
            is_gray = True
        elif img.mode == "RGBA":
            img = img.convert("RGB"); is_gray = False
        elif img.mode != "RGB":
            img = img.convert("RGB"); is_gray = False
        else:
            is_gray = False
    except Exception as e:
        print(f"[WARN] JPEG decode failed: {e}")
        return

    # --- draw ---
    ax.clear()
    if is_gray:
        # 2D grayscale: show with gray colormap (avoid viridis)
        ax.imshow(img, cmap="gray", vmin=0, vmax=255, interpolation="nearest")
    else:
        # True color: no colormap
        ax.imshow(img, interpolation="nearest")

    avg_kb = kb(sum(sizes) / len(sizes)) if sizes else 0.0
    cur_kb = kb(len(payload))
    ax.set_title(f"{img.width}x{img.height}  |  size {cur_kb:.1f} KB  (avg {avg_kb:.1f} KB / {len(sizes)} frames)")
    ax.axis("off")
    fig.canvas.draw_idle()
    plt.pause(0.001)

    if frame_count % 10 == 0:
        print(f"[FRAME {frame_count}] {img.width}x{img.height}  size={cur_kb:.1f} KB  avg100={avg_kb:.1f} KB")

def build_client() -> mqtt.Client:
    c = mqtt.Client(client_id="viewer-Device01", clean_session=True)
    c.username_pw_set(MQTT_USER, MQTT_PASS)
    ctx = ssl.create_default_context(cafile=certifi.where())
    ctx.check_hostname = True
    ctx.verify_mode = ssl.CERT_REQUIRED
    c.tls_set_context(ctx)
    c.on_connect = on_connect
    c.on_message = on_message
    return c

def main():
    global fig, ax
    client = build_client()
    client.connect(MQTT_HOST, MQTT_PORT, keepalive=60)

    plt.ion()
    fig, ax = plt.subplots(num="ESP32-S3 JPEG stream")
    ax.axis("off")
    ax.set_title("Waiting for frames…")

    while True:
        client.loop(timeout=0.05)
        plt.pause(0.01)

if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\nExiting…")
