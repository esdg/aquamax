import hashlib
import os
from pathlib import Path
import re
import shutil
import subprocess

Import("env")

root = Path(env.subst("$PROJECT_DIR"))
source = root / "assets" / "icons"
generator = root / "web" / "scripts" / "generate-icons.mjs"
output = root / "src" / "generated" / "icons.h"
names = ("thermometer", "drop", "three-drops", "warning", "thermometer-off", "drop-off", "three-drops-off")
raw_sizes = os.environ.get("AQUAMAX_ICON_SIZES", "32,48")
try:
    sizes = [int(value) for value in raw_sizes.split(",")]
    if not sizes or len(set(sizes)) != len(sizes) or any(size < 8 or size > 128 for size in sizes):
        raise ValueError()
except ValueError:
    raise RuntimeError("AQUAMAX_ICON_SIZES must be unique comma-separated sizes from 8 to 128")

fingerprint = hashlib.sha256()
fingerprint.update(",".join(str(size) for size in sizes).encode("ascii"))
fingerprint.update(generator.read_bytes())
for name in names:
    path = source / (name + ".svg")
    if not path.is_file():
        raise RuntimeError("Required icon is missing: " + str(path))
    fingerprint.update(name.encode("ascii"))
    fingerprint.update(path.read_bytes())
digest = fingerprint.hexdigest()
existing = output.read_text(encoding="utf-8") if output.is_file() else ""
match = re.search(r"^// source-sha256: ([0-9a-f]{64})$", existing, re.MULTILINE)
if not match or match.group(1) != digest:
    node = shutil.which("node")
    if not node:
        raise RuntimeError("Icon assets changed; install Node.js and run npm ci --prefix web")
    print("Generating display icons from SVG assets")
    subprocess.check_call([node, str(generator), digest, *[str(size) for size in sizes]], cwd=str(root))