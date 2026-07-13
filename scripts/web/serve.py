#!/usr/bin/env python3
"""Serves a web build directory and collects boot logs POSTed by the dev
shell at /log. Usage: serve.py [port] (run from the build directory)."""
import http.server
import sys


class Handler(http.server.SimpleHTTPRequestHandler):
    def do_POST(self):
        n = int(self.headers.get("Content-Length", 0))
        body = self.rfile.read(n).decode("utf-8", "replace")
        if self.path == "/log":
            with open("boot.log", "a") as f:
                f.write(body + "\n")
        elif self.path == "/shot":
            # Body is a canvas data URL; store the decoded PNG.
            import base64
            _, b64 = body.split(",", 1)
            with open("shot.png", "wb") as f:
                f.write(base64.b64decode(b64))
        else:
            self.send_error(404)
            return
        self.send_response(204)
        self.end_headers()

    def log_message(self, *args):
        pass


port = int(sys.argv[1]) if len(sys.argv) > 1 else 8123
http.server.ThreadingHTTPServer(("", port), Handler).serve_forever()
