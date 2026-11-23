"""Train a Neural Amp Modeler (NAM) model from a GigRig amp capture.

    python train_from_capture.py "<capture>.wav" <out-folder> [epochs]

The capture is what the app's Amp capture tab writes: a 2-channel WAV, channel 1 = the test
sound as sent, channel 2 = what came back from the amp. This script:
  1. resamples both channels to 48 kHz (the rate the model runs at inside the app),
  2. finds the round-trip delay from the first test click (0.5 s in),
  3. trains a standard NAM WaveNet on everything before 129 s and checks it on the rest
     (the repeated plucked notes, which it never learns from),
  4. writes model.nam into the out folder, plus the configs NAM used.

Set up once (see docs/effects.md, "Training an amp model"):
    python -m venv venv
    venv/bin/pip install "neural-amp-modeler==0.10.0" "setuptools<70" soundfile scipy
On a machine with no display, put tools/nam/stub on PYTHONPATH (a stand-in for tkinter).

Why neural-amp-modeler 0.10.0: it exports the classic .nam format (version 0.5.4) that the
NAM core built into the app (v0.3.0) reads. Newer trainers use a different model layout.
"""

import sys
from pathlib import Path

import numpy as np
import soundfile as sf
from scipy.signal import resample_poly

RATE = 48000
SPLIT_SECONDS = 129          # train before this, check after it


def prepare(capture: Path, work: Path) -> int:
    x, rate = sf.read(str(capture), dtype="float64")
    if x.ndim != 2 or x.shape[1] < 2:
        raise SystemExit("The capture must have two channels (sent, came back).")
    if rate % RATE == 0:
        a, b = (resample_poly(x[:, k], 1, int(rate // RATE)) for k in (0, 1))
    else:
        a, b = (resample_poly(x[:, k], RATE, int(rate)) for k in (0, 1))

    # The delay: where the first click starts on the way out, and where it arrives back.
    sent = int(np.nonzero(np.abs(a) > 1e-3 * np.max(np.abs(a)))[0][0])
    noise = np.max(np.abs(b[: int(0.4 * RATE)]))
    back = int(np.nonzero(np.abs(b) > 4.0 * noise)[0][0])
    delay = back - sent
    if not 0 <= delay < RATE // 10:
        raise SystemExit(f"Couldn't find the click coming back (delay {delay} samples): is channel 2 the amp?")

    work.mkdir(parents=True, exist_ok=True)
    sf.write(str(work / "x.wav"), a.astype(np.float32), RATE, subtype="PCM_24")
    sf.write(str(work / "y.wav"), b.astype(np.float32), RATE, subtype="PCM_24")
    print(f"{capture.name}: {len(a) / RATE:.1f} s, delay {delay} samples ({delay / RATE * 1000:.2f} ms)")
    return delay


def train(work: Path, out: Path, delay: int, epochs: int) -> None:
    import torch
    from nam.train import full

    torch.set_num_threads(max(1, torch.get_num_threads()))
    dilations = [1, 2, 4, 8, 16, 32, 64, 128, 256, 512]
    model = {
        "net": {"name": "WaveNet", "config": {"layers_configs": [
            {"input_size": 1, "condition_size": 1, "channels": 16, "head_size": 8, "kernel_size": 3,
             "dilations": dilations, "activation": "Tanh", "gated": False, "head_bias": False},
            {"input_size": 16, "condition_size": 1, "channels": 8, "head_size": 1, "kernel_size": 3,
             "dilations": dilations, "activation": "Tanh", "gated": False, "head_bias": True}],
            "head_scale": 0.02}},
        "loss": {"val_loss": "esr", "mask_first": 4096, "pre_emph_weight": 1.0, "pre_emph_coef": 0.85},
        "optimizer": {"lr": 0.004},
        "lr_scheduler": {"class": "ExponentialLR", "kwargs": {"gamma": 0.993}},
    }
    split = SPLIT_SECONDS * RATE
    data = {
        "train": {"stop_samples": split, "ny": 8192},
        "validation": {"start_samples": split, "ny": None, "require_input_pre_silence": False},
        "common": {"x_path": str(work / "x.wav"), "y_path": str(work / "y.wav"), "delay": delay},
    }
    learning = {
        "train_dataloader": {"batch_size": 16, "shuffle": True, "pin_memory": False,
                             "drop_last": True, "num_workers": 0},
        "val_dataloader": {},
        "trainer": {"max_epochs": epochs, "accelerator": "auto", "enable_progress_bar": False},
    }
    out.mkdir(parents=True, exist_ok=True)
    full.main(data, model, learning, out, no_show=True, make_plots=False)


if __name__ == "__main__":
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    capture, out = Path(sys.argv[1]), Path(sys.argv[2])
    epochs = int(sys.argv[3]) if len(sys.argv) > 3 else 100
    delay = prepare(capture, out / "data")
    train(out / "data", out, delay, epochs)
    print("Wrote", out / "model.nam")
