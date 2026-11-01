"""HTTP control and streaming routes for receiver subsystems."""
import json
import urllib.parse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

from receiver_app.services import ethernet, radio, television


class ReceiverHandler(BaseHTTPRequestHandler):
    server_version = "TVReceiver/1.0"
    config = None

    def send_json(self, status, obj):
        data = json.dumps(obj).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def stream(self, source):
        self.send_response(200)
        self.send_header("Content-Type", "video/mp2t")
        self.send_header("Cache-Control", "no-store")
        self.send_header("Connection", "close")
        self.end_headers()
        try:
            while True:
                chunk = source.read()
                if not chunk:
                    break
                self.wfile.write(chunk)
        except (BrokenPipeError, ConnectionResetError, OSError):
            pass
        finally:
            source.close()

    def do_GET(self):
        route = urllib.parse.urlparse(self.path)
        query = urllib.parse.parse_qs(route.query)
        if route.path == "/status":
            self.send_json(200, {
                "television": television.status(self.config["television"]),
                "radio": radio.status(self.config["radio"]),
                "ethernet": ethernet.status(),
                "capabilities": ["DVB-T", "DVB-T2", "DVB-C", "FM", "IP multicast MPEG-TS"],
            })
        elif route.path == "/tv/status":
            self.send_json(200, television.status(self.config["television"]))
        elif route.path == "/radio/status":
            self.send_json(200, radio.status(self.config["radio"]))
        elif route.path == "/ethernet/status":
            self.send_json(200, ethernet.status())
        elif route.path == "/tv.ts":
            try:
                self.stream(television.open_channel(query.get("channel", [""])[0], self.config["television"]))
            except ValueError as error:
                self.send_json(400, {"error": str(error)})
            except (OSError, RuntimeError) as error:
                self.send_json(503, {"error": str(error)})
        elif route.path == "/udp.ts":
            try:
                group = query.get("group", [""])[0]
                port = int(query.get("port", ["1234"])[0])
                self.stream(ethernet.open_multicast(group, port, self.config["ethernet"]))
            except ValueError as error:
                self.send_json(400, {"error": str(error) or "Invalid UDP port"})
            except OSError as error:
                self.send_json(503, {"error": str(error)})
        else:
            self.send_json(404, {"error": "Routes: /status, /tv/status, /radio/status, /ethernet/status, /tv.ts, /udp.ts"})

    def do_POST(self):
        if self.path != "/radio/tune":
            return self.send_json(404, {"error": "Route not found"})
        try:
            size = int(self.headers.get("Content-Length", "0"))
            if size < 1:
                return self.send_json(400, {"error": "A JSON request body is required"})
            if size > 4096:
                return self.send_json(413, {"error": "Request too large"})
            body = json.loads(self.rfile.read(size))
            frequency = float(body["frequency_mhz"])
            self.send_json(200, radio.tune(frequency, self.config["radio"]))
        except (ValueError, KeyError, TypeError, json.JSONDecodeError) as error:
            self.send_json(400, {"error": str(error)})
        except (OSError, RuntimeError, TimeoutError) as error:
            self.send_json(503, {"error": str(error)})

    def log_message(self, fmt, *args):
        print("receiver:", fmt % args, flush=True)


def serve(config):
    handler = type("ConfiguredReceiverHandler", (ReceiverHandler,), {"config": config})
    api_config = config.get("api", {})
    address = (api_config.get("listen_address", "0.0.0.0"), int(api_config.get("listen_port", 8080)))
    print(f"Receiver API listening on {address[0]}:{address[1]}", flush=True)
    ThreadingHTTPServer(address, handler).serve_forever()
