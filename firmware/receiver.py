#!/usr/bin/env python3
"""Small Linux receiver control and MPEG-TS streaming service."""
import json
import ipaddress
import os
import shutil
import socket
import subprocess
import urllib.parse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

CONFIG = os.environ.get("RECEIVER_CONFIG", "/etc/receiver/receiver.json")
with open(CONFIG, encoding="utf-8") as f:
    CFG = json.load(f)


def require_tool(name):
    path = shutil.which(name)
    if not path:
        raise RuntimeError(f"Required system program is missing: {name}")
    return path


class Handler(BaseHTTPRequestHandler):
    server_version = "TVReceiver/1.0"

    def send_json(self, status, obj):
        data = json.dumps(obj).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        route = urllib.parse.urlparse(self.path)
        q = urllib.parse.parse_qs(route.query)
        if route.path == "/status":
            self.send_json(200, {"dvb_frontend": os.path.exists("/dev/dvb/adapter0/frontend0"),
                                 "fm_radio": os.path.exists(CFG["radio_device"]),
                                 "ethernet": socket.if_nameindex(),
                                 "capabilities": ["DVB-T", "DVB-T2", "DVB-C", "FM", "IP multicast MPEG-TS"]})
        elif route.path == "/tv.ts":
            name = q.get("channel", [""])[0]
            if not name or len(name) > 128:
                return self.send_json(400, {"error": "Pass a channel name as ?channel=NAME"})
            try:
                zap = require_tool("dvbv5-zap")
            except RuntimeError as e:
                return self.send_json(503, {"error": str(e)})
            self.stream_process([zap, "-a", "0", "-c", CFG["channels_file"], "-o", "-", name])
        elif route.path == "/udp.ts":
            try:
                group = q.get("group", [""])[0]
                port = int(q.get("port", ["1234"])[0])
                address = ipaddress.ip_address(group)
                if not (address.version == 4 and address in ipaddress.ip_network("239.0.0.0/8") and 1 <= port <= 65535):
                    raise ValueError()
                self.stream_multicast(group, port)
            except ValueError:
                self.send_json(400, {"error": "Use a multicast group in 239.0.0.0/8 and a valid UDP port"})
        else:
            self.send_json(404, {"error": "Routes: /status, /tv.ts?channel=NAME, /udp.ts?group=239.x.x.x&port=1234"})

    def do_POST(self):
        if self.path != "/radio/tune":
            return self.send_json(404, {"error": "Route not found"})
        try:
            size = int(self.headers.get("Content-Length", "0"))
            if size > 4096:
                return self.send_json(413, {"error": "Request too large"})
            body = json.loads(self.rfile.read(size))
            mhz = float(body["frequency_mhz"])
            low, high = CFG.get("fm_range_mhz", [87.5, 108.0])
            if not low <= mhz <= high:
                return self.send_json(400, {"error": f"FM frequency must be {low}-{high} MHz"})
            subprocess.run([require_tool("v4l2-ctl"), "-d", CFG["radio_device"], f"--set-freq={mhz:.2f}"], check=True, timeout=5)
            self.send_json(200, {"frequency_mhz": mhz, "audio": "FM tuner analog audio output; connect to an amplifier or external line input"})
        except (ValueError, KeyError, json.JSONDecodeError, subprocess.SubprocessError, RuntimeError) as e:
            self.send_json(400, {"error": str(e)})

    def stream_process(self, command):
        try:
            proc = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
        except (OSError, RuntimeError) as e:
            return self.send_json(503, {"error": str(e)})
        self.send_response(200)
        self.send_header("Content-Type", "video/mp2t")
        self.send_header("Cache-Control", "no-store")
        self.send_header("Connection", "close")
        self.end_headers()
        try:
            while True:
                chunk = proc.stdout.read(188 * 128)
                if not chunk:
                    break
                self.wfile.write(chunk)
        except (BrokenPipeError, ConnectionResetError):
            pass
        finally:
            proc.terminate()
            try:
                proc.wait(timeout=2)
            except subprocess.TimeoutExpired:
                proc.kill()

    def stream_multicast(self, group, port):
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
        try:
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            sock.bind(("", port))
            membership = socket.inet_aton(group) + socket.inet_aton(CFG.get("multicast_interface", "0.0.0.0"))
            sock.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, membership)
        except OSError as e:
            sock.close()
            return self.send_json(503, {"error": str(e)})
        self.send_response(200)
        self.send_header("Content-Type", "video/mp2t")
        self.send_header("Connection", "close")
        self.end_headers()
        try:
            while True:
                data, _ = sock.recvfrom(65536)
                self.wfile.write(data)
        except (BrokenPipeError, ConnectionResetError, OSError):
            pass
        finally:
            sock.close()

    def log_message(self, fmt, *args):
        print("receiver:", fmt % args, flush=True)


if __name__ == "__main__":
    address = (CFG.get("listen_address", "0.0.0.0"), int(CFG.get("listen_port", 8080)))
    print(f"Receiver API listening on {address[0]}:{address[1]}", flush=True)
    ThreadingHTTPServer(address, Handler).serve_forever()
