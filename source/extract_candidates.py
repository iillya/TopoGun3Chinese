"""Read-only binary inventory; extracted strings are candidates, not proven UI."""
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
host = Path(r"C:\Program Files\TopoGun 3")
data = (host / "TopoGun.exe").read_bytes()
strings = {m.group().decode("ascii") for m in re.finditer(rb"[\x20-\x7e]{3,180}", data)}
candidates = sorted(s for s in strings if re.fullmatch(r"[A-Za-z&][A-Za-z0-9 &()/.,:+!?_'%-]{2,120}", s)
                    and not any(x in s for x in ("::", "%", "http", ".dll", ".cpp", "__")))
out = ROOT / "build" / "extraction"
out.mkdir(parents=True, exist_ok=True)
(out / "ascii_candidates.json").write_text(json.dumps(candidates, ensure_ascii=False, indent=2), encoding="utf-8")
print(f"ASCII candidates: {len(candidates)}; output: {out}")
