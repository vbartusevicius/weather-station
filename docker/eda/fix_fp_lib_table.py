from pathlib import Path

for p in (Path("/root/.config/kicad/10.0/fp-lib-table"), Path("/root/.config/kicad/fp-lib-table")):
    if p.exists():
        p.write_text(p.read_text().replace('"', ""))
