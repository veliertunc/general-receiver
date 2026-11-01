"""DVB television status and MPEG transport stream acquisition."""
import os
import shutil
import subprocess

from settings import CONFIG


class TelevisionStream:
    def __init__(self, channel):
        if not channel or len(channel) > 128:
            raise ValueError("Pass a channel name (up to 128 characters)")
        tool = shutil.which("dvbv5-zap")
        if not tool:
            raise RuntimeError("Required system program is missing: dvbv5-zap")
        self.process = subprocess.Popen(
            [tool, "-a", "0", "-c", CONFIG["channels_file"], "-o", "-", channel],
            stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)

    def read(self, size=188 * 128):
        return self.process.stdout.read(size)

    def close(self):
        if self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(timeout=2)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait()


def status():
    frontend = "/dev/dvb/adapter0/frontend0"
    return {"frontend": frontend, "available": os.path.exists(frontend),
            "channels_file": CONFIG["channels_file"],
            "standards": ["DVB-T", "DVB-T2", "DVB-C"]}


def open_channel(channel):
    return TelevisionStream(channel)
