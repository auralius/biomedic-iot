#!/usr/bin/env python3
# Super-simple RTP ECG receiver (1 Hz, autoscaling) with 'q' to quit

import socket, struct, time
import numpy as np
import matplotlib.pyplot as plt

# ========= CONFIG =========
BIND_HOST        = "0.0.0.0"
BIND_PORT        = 6000
SAMPLES_PER_PKT  = 250
WINDOW_SECONDS   = 10.0
PRINT_EVERY_N    = 5
# ==========================

RTP_HDR = struct.Struct('!BBHII')
RTP_HDR_LEN = RTP_HDR.size
DTYPE = np.dtype('<i2')
BYTES_PER_PKT = SAMPLES_PER_PKT * DTYPE.itemsize

FS_HZ = SAMPLES_PER_PKT
WIN_SAMPLES = int(WINDOW_SECONDS * FS_HZ)

x = np.linspace(-WINDOW_SECONDS, 0, WIN_SAMPLES, dtype=np.float32)
y = np.zeros(WIN_SAMPLES, dtype=np.float32)

fig = plt.figure(figsize=(10,4))
ax = fig.add_subplot(111)
(line,) = ax.plot(x, y)
ax.set_title("RTP ECG (µV)")
ax.set_xlabel("Time (s)")
ax.set_ylabel("µV")
ax.grid(True, alpha=0.3)
info = ax.text(0.01, 0.95, "", transform=ax.transAxes, va="top")

# --- Quit handling: 'q' or window close ---
running = True
def _on_key(evt):
    global running
    if evt.key and evt.key.lower() == 'q':
        running = False
        plt.close(fig)           # close the figure immediately

def _on_close(evt):
    global running
    running = False

fig.canvas.mpl_connect('key_press_event', _on_key)
fig.canvas.mpl_connect('close_event', _on_close)

# Socket
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
try:
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 1 << 20)
except OSError:
    pass
sock.bind((BIND_HOST, BIND_PORT))
sock.settimeout(1.0)

print(f"Listening on {BIND_HOST}:{BIND_PORT} — expecting 1 RTP packet/s with {SAMPLES_PER_PKT} samples")

pkts = 0
lost = 0
exp_seq = None
t0 = time.time()

try:
    while running:
        # If the user closed the plot, also bail out
        if not plt.fignum_exists(fig.number):
            break

        try:
            data, _ = sock.recvfrom(4096)
        except socket.timeout:
            # let UI breathe while idle, and also allow 'q' to be processed
            plt.pause(0.01)
            continue

        if len(data) < RTP_HDR_LEN:
            continue

        vpxcc, mpt, seq, ts, ssrc = RTP_HDR.unpack_from(data, 0)
        v    = (vpxcc >> 6) & 0x03
        xbit = (vpxcc >> 4) & 0x01
        cc   =  vpxcc       & 0x0F
        if v != 2 or cc != 0 or xbit != 0:
            continue

        payload = data[RTP_HDR_LEN:]
        if len(payload) != BYTES_PER_PKT:
            print(f"warn: payload {len(payload)}B != expected {BYTES_PER_PKT}B")
            if len(payload) < 2:
                continue

        arr = np.frombuffer(payload, dtype=DTYPE, count=SAMPLES_PER_PKT).astype(np.float32, copy=True)

        # Stats
        pkts += 1
        if exp_seq is None:
            exp_seq = (seq + 1) & 0xFFFF
        else:
            expect = exp_seq
            if seq != expect:
                gap = (seq - expect) & 0xFFFF
                if gap != 0:
                    lost += gap
            exp_seq = (seq + 1) & 0xFFFF

        # Plot update
        y = np.roll(y, -SAMPLES_PER_PKT)
        y[-SAMPLES_PER_PKT:] = arr
        line.set_ydata(y)
        ax.relim(); ax.autoscale_view()

        elapsed = max(1e-6, time.time() - t0)
        rate = pkts / elapsed
        info.set_text(f"pkts={pkts} lost={lost} rate={rate:.1f} pkt/s")

        if PRINT_EVERY_N and (pkts % PRINT_EVERY_N == 0):
            print(f"n={arr.size}  min={arr.min():.0f}  max={arr.max():.0f}  mean={arr.mean():.1f} uV")

        # Let matplotlib process key/close events
        plt.pause(0.01)

except KeyboardInterrupt:
    pass
finally:
    sock.close()
    print("Bye")
