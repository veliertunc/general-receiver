"""DVB television status and MPEG transport stream acquisition."""
import os
import shutil
import subprocess


class TelevisionStream:
    def __init__(self, channel, config):
        if not channel or len(channel) > 128:
            raise ValueError("Pass a channel name (up to 128 characters)")
        tool = shutil.which("dvbv5-zap")
        if not tool:
            raise RuntimeError("Required system program is missing: dvbv5-zap")
        self.process = subprocess.Popen(
            [tool, "-a", str(config.get("adapter", 0)), "-c", config["channels_file"], "-o", "-", channel],
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


def status(config):
    frontend = f"/dev/dvb/adapter{config.get('adapter', 0)}/frontend0"
    return {"frontend": frontend, "available": os.path.exists(frontend),
            "channels_file": config["channels_file"],
            "standards": config.get("standards", ["DVB-T", "DVB-T2", "DVB-C"])}


def open_channel(channel, config):
    return TelevisionStream(channel, config)
