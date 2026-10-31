#!/usr/bin/env python3
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse
import json
import sys

HERE = Path(__file__).resolve().parent
REPORTS = HERE / "captured"
REPORTS.mkdir(exist_ok=True)
HEADER_LOG = REPORTS / "headers.jsonl"


class Handler(SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=str(HERE), **kwargs)

    def log_message(self, format, *args):
        sys.stderr.write("%s - %s\n" % (self.headers.get("Host", ""), format % args))

    def _record_headers(self, kind):
        parsed = urlparse(self.path)
        entry = {
            "kind": kind,
            "host": self.headers.get("Host", ""),
            "path": parsed.path,
            "userAgent": self.headers.get("User-Agent"),
            "secChUa": self.headers.get("Sec-CH-UA"),
            "secChUaMobile": self.headers.get("Sec-CH-UA-Mobile"),
            "secChUaPlatform": self.headers.get("Sec-CH-UA-Platform"),
            "acceptCh": self.headers.get("Accept-CH"),
            "criticalCh": self.headers.get("Critical-CH"),
        }
        with HEADER_LOG.open("a") as fh:
            fh.write(json.dumps(entry) + "\n")
        return entry

    def do_GET(self):
        parsed = urlparse(self.path)
        self._record_headers("GET")
        if parsed.path == "/echo-headers":
            body = json.dumps(self._record_headers("echo")).encode()
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Access-Control-Allow-Origin", "*")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return
        if parsed.path == "/pixel.png":
            self.send_response(200)
            self.send_header("Content-Type", "image/png")
            self.send_header("Content-Length", "0")
            self.end_headers()
            return
        super().do_GET()

    def do_OPTIONS(self):
        self.send_response(204)
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Headers", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
        self.end_headers()

    def do_POST(self):
        parsed = urlparse(self.path)
        if parsed.path == "/echo-headers":
            body = json.dumps(self._record_headers("POST")).encode()
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Access-Control-Allow-Origin", "*")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return
        if parsed.path != "/report":
            self.send_error(404)
            return
        length = int(self.headers.get("Content-Length", "0"))
        body = json.loads(self.rfile.read(length).decode())
        run = parse_qs(parsed.query).get("run", [body.get("runId", "run")])[0]
        path = REPORTS / f"{run}.json"
        if path.exists():
            try:
                data = json.loads(path.read_text())
            except json.JSONDecodeError:
                data = {"runId": run, "events": [], "reads": {}}
        else:
            data = {"runId": run, "events": [], "reads": {}}
        data.setdefault("events", []).append(body)
        data.setdefault("reads", {})
        if "key" in body and "value" in body:
            data["reads"][body["key"]] = body["value"]
        if body.get("key") == "complete":
            data["complete"] = True
        path.write_text(json.dumps(data, indent=2) + "\n")
        self.send_response(204)
        self.end_headers()


if __name__ == "__main__":
    HEADER_LOG.write_text("")
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8080
    ThreadingHTTPServer(("0.0.0.0", port), Handler).serve_forever()
