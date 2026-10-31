import argparse
import secrets
from pathlib import Path
from urllib.parse import urlsplit
from urllib.request import Request, urlopen


def main():
    parser = argparse.ArgumentParser(description="Upload dashboard assets without replacing LittleFS history.")
    parser.add_argument("--url", default="http://aquamax.local")
    arguments = parser.parse_args()
    url = urlsplit(arguments.url)
    if url.scheme != "http" or not url.hostname or url.username or url.password or url.path not in ("", "/") or url.query or url.fragment or url.port not in (None, 80):
        parser.error("Use the device's HTTP hostname or IP address without a path or nonstandard port.")
    origin = f"http://{url.hostname}"
    assets = Path(__file__).resolve().parent.parent / "data"
    for name in ("app.css.gz", "app.js.gz", "manifest.webmanifest.gz", "icon-192.png.gz", "icon-512.png.gz", "logo-aquamax.svg.gz", "jumbo.jpg.gz", "index.html.gz", "service-worker.js.gz"):
        content = (assets / name).read_bytes()
        if not content.startswith(b"\x1f\x8b") or len(content) > 262144:
            raise ValueError(f"Invalid or oversized asset: {name}")
        boundary = "aquamax" + secrets.token_hex(16)
        prefix = (f"--{boundary}\r\nContent-Disposition: form-data; name=\"file\"; filename=\"{name}\"\r\n"
                  "Content-Type: application/gzip\r\n\r\n").encode("ascii")
        payload = prefix + content + f"\r\n--{boundary}--\r\n".encode("ascii")
        request = Request(origin + "/api/dashboard-assets", data=payload, method="POST", headers={
            "Origin": origin,
            "Content-Type": f"multipart/form-data; boundary={boundary}",
        })
        with urlopen(request, timeout=30) as response:
            if response.status != 200:
                raise RuntimeError(f"Asset upload failed: {name}")
        print(f"Uploaded {name}")


if __name__ == "__main__":
    main()