from pathlib import Path

root = Path(__file__).resolve().parents[1] if '__file__' in globals() else Path.cwd()
page = (root / 'web/index.html').read_text(encoding='utf-8')
assert ')AQWEB"' not in page
assert len(page.encode('utf-8')) < 11800, 'HTTP response buffer too small'
target = root / 'firmware/pico/include/web_page.h'
target.write_text('#pragma once\nstatic const char WEB_PAGE[] = R"AQWEB(' + page + ')AQWEB";\n', encoding='utf-8')
