"""FM tuner controls, independent of the HTTP server."""
import shutil
import subprocess

from settings import CONFIG


def status():
    import os
    device = CONFIG["radio_device"]
    return {"device": device, "available": os.path.exists(device)}


def tune(frequency_mhz):
    low, high = CONFIG.get("fm_range_mhz", [87.5, 108.0])
    if not low <= frequency_mhz <= high:
        raise ValueError(f"FM frequency must be {low}-{high} MHz")
    tool = shutil.which("v4l2-ctl")
    if not tool:
        raise RuntimeError("Required system program is missing: v4l2-ctl")
    try:
        subprocess.run([tool, "-d", CONFIG["radio_device"], f"--set-freq={frequency_mhz:.2f}"],
                       check=True, timeout=5, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
    except subprocess.CalledProcessError as error:
        raise RuntimeError(error.stderr.strip() or "FM tuner rejected the requested frequency") from error
    except subprocess.TimeoutExpired as error:
        raise RuntimeError("FM tuner command timed out") from error
    return {"frequency_mhz": frequency_mhz,
            "audio": "FM tuner analog audio output; connect to an amplifier or external line input"}
