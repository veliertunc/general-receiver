"""DVB television status and MPEG transport stream acquisition."""
import os
import shutil
import subprocess
import threading

_tuners = set()
_tuners_lock = threading.Lock()


class TunerBusyError(RuntimeError):
    pass


def validate_config(config):
    adapter = config.get("adapter", 0)
    if isinstance(adapter, bool) or not isinstance(adapter, int) or adapter < 0:
        raise ValueError("adapter must be a non-negative integer")
    channels_file = config.get("channels_file")
    if not isinstance(channels_file, str) or not channels_file.startswith("/"):
        raise ValueError("channels_file must be an absolute path")


class TelevisionStream:
    def __init__(self, channel, config):
        if not channel or len(channel) > 128:
            raise ValueError("Pass a channel name (up to 128 characters)")
        tool = shutil.which("dvbv5-zap")
        if not tool:
            raise RuntimeError("Required system program is missing: dvbv5-zap")
        self.adapter = config.get("adapter", 0)
        self.closed = False
        with _tuners_lock:
            if self.adapter in _tuners:
                raise TunerBusyError("The television tuner is already streaming")
            _tuners.add(self.adapter)
        try:
            self.process = subprocess.Popen(
                [tool, "-a", str(self.adapter), "-c", config["channels_file"], "-o", "-", channel],
                stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
        except Exception:
            with _tuners_lock:
                _tuners.discard(self.adapter)
            raise

    def read(self, size=188 * 128):
        return self.process.stdout.read(size)

    def close(self):
        if self.closed:
            return
        self.closed = True
        try:
            if self.process.poll() is None:
                self.process.terminate()
                try:
                    self.process.wait(timeout=2)
                except subprocess.TimeoutExpired:
                    self.process.kill()
                    self.process.wait()
            self.process.stdout.close()
        finally:
            with _tuners_lock:
                _tuners.discard(self.adapter)


def status(config):
    frontend = f"/dev/dvb/adapter{config.get('adapter', 0)}/frontend0"
    return {"frontend": frontend, "available": os.path.exists(frontend),
            "channels_file": config["channels_file"],
            "standards": config.get("standards", ["DVB-T", "DVB-T2", "DVB-C"])}


def open_channel(channel, config):
    return TelevisionStream(channel, config)
