"""FM tuner status and controls."""
import os
import shutil
import subprocess


def validate_config(config):
    device = config.get("device")
    if not isinstance(device, str) or not device.startswith("/"):
        raise ValueError("device must be an absolute device path")
    band = config.get("fm_range_mhz", [87.5, 108.0])
    if not isinstance(band, list) or len(band) != 2:
        raise ValueError("fm_range_mhz must contain a lower and upper frequency")
    if any(isinstance(value, bool) or not isinstance(value, (int, float)) for value in band):
        raise ValueError("fm_range_mhz values must be numbers")
    low, high = map(float, band)
    if not 0 < low < high:
        raise ValueError("fm_range_mhz must be an increasing positive range")


def status(config):
    device = config["device"]
    return {"device": device, "available": os.path.exists(device)}


def tune(frequency_mhz, config):
    low, high = config.get("fm_range_mhz", [87.5, 108.0])
    if not low <= frequency_mhz <= high:
        raise ValueError(f"FM frequency must be {low}-{high} MHz")
    tool = shutil.which("v4l2-ctl")
    if not tool:
        raise RuntimeError("Required system program is missing: v4l2-ctl")
    try:
        subprocess.run([tool, "-d", config["device"], f"--set-freq={frequency_mhz:.2f}"],
                       check=True, timeout=5, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
    except subprocess.CalledProcessError as error:
        raise RuntimeError(error.stderr.strip() or "FM tuner rejected the requested frequency") from error
    except subprocess.TimeoutExpired as error:
        raise RuntimeError("FM tuner command timed out") from error
    return {"frequency_mhz": frequency_mhz,
            "audio": "FM tuner analog audio output; connect to an amplifier or external line input"}
