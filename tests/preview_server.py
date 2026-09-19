"""Local-only UI test fixture; not firmware and never deployed."""
import http.server
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]/'web'
STATUS = dict(sync=True, link_fresh=True, utc_us=1789776000250000, nav_utc_s=1789776000,
              fix=3, satellites=12, lat=35.0, lon=139.0, position_valid=True,
              pps_count=123, pps_period_us=1000001, uart_age_us=231000, crc_errors=0,
              sync_rejected=0, power_ready=True, bus_v=11.8, current_a=0.21,
              sd_verified=True, logging=True, file='P00001.CSV', rows=1200,
              sd_errors=0, sd_error='NONE', missed_rows=0, max_write_us=3500,
              heap_free=320000, sp_sd_ok=True, sp_imu_ok=True, sp_imu_samples=14400,
              sp_imu_errors=0, sp_imu_gaps=0, sp_sd_errors=0)


class Handler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=str(ROOT), **kwargs)

    def json(self, value):
        body=json.dumps(value).encode()
        self.send_response(200)
        self.send_header('Content-Type', 'application/json')
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        if self.path == '/api/status':
            return self.json(STATUS)
        if self.path.startswith('/api/logs'):
            return self.json(dict(files=[dict(name='P00000.CSV', bytes=1500, active=False),
                                         dict(name='P00001.CSV', bytes=34500, active=STATUS['logging'])]))
        return super().do_GET()

    def do_POST(self):
        if self.path == '/api/stop':
            STATUS['logging']=False
        elif self.path == '/api/start':
            STATUS['logging']=True
        return self.json(dict(ok=True))


if __name__ == '__main__':
    http.server.ThreadingHTTPServer(('127.0.0.1', 8765), Handler).serve_forever()
