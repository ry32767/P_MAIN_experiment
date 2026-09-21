from http.server import BaseHTTPRequestHandler,HTTPServer
from pathlib import Path
r=Path(__file__).resolve().parents[1]
class Handler(BaseHTTPRequestHandler):
 def do_GET(self):
  if self.path=='/api/status':data=(r/'reports/raw/web_live_status.json').read_bytes();typ='application/json'
  elif self.path=='/':data=(r/'web/index.html').read_bytes().replace(b'<header>',b'<header><p>DEVICE RESPONSE REPLAY / 2026-09-20 UI verification</p>');typ='text/html; charset=utf-8'
  else:self.send_error(404);return
  self.send_response(200);self.send_header('Content-Type',typ);self.end_headers();self.wfile.write(data)
 def log_message(self,*args):pass
HTTPServer(('127.0.0.1',8776),Handler).serve_forever()
