#!/usr/bin/env python3
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse
import json
import sys

HERE = Path(__file__).resolve().parent
REPORTS = HERE / "captured"
REPORTS.mkdir(exist_ok=True)


class Handler(SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=str(HERE), **kwargs)

    def log_message(self, format, *args):
        sys.stderr.write("%s - %s\n" % (self.headers.get("Host", ""), format % args))

    def do_POST(self):
        parsed = urlparse(self.path)
        if parsed.path != "/report":
            self.send_error(404)
            return
        length = int(self.headers.get("Content-Length", "0"))
        body = json.loads(self.rfile.read(length).decode())
        run = parse_qs(parsed.query).get("run", [body.get("runId", "run")])[0]
        path = REPORTS / f"{run}.json"
        if "samples" in body:
            path.write_text(json.dumps(body, indent=2) + "\n")
            self.send_response(204)
            self.end_headers()
            return
        if path.exists():
            try:
                data = json.loads(path.read_text())
            except json.JSONDecodeError:
                data = {"runId": run, "events": [], "reads": {}}
        else:
            data = {"runId": run, "events": [], "reads": {}}
        if "events" not in data:
            data = {"runId": run, "events": [data], "reads": {}}
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
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8080
    ThreadingHTTPServer(("0.0.0.0", port), Handler).serve_forever()
