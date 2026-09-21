from pathlib import Path
Import("env")
r=Path(env.subst("$PROJECT_DIR"))
p=(r/'web/index.html').read_text(encoding='utf-8')
assert ')AQWEB"' not in p
(r/'include/web_page.h').write_text('#pragma once\nstatic const char WEB_PAGE[]=R"AQWEB('+p+')AQWEB";\n',encoding='utf-8')
