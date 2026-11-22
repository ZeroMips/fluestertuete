#!/usr/bin/env python3
"""Checks the card files from tests/ripstation_files.mjs: YAML that cloud-init accepts, values intact, and
station.conf as setup-station.py reads it.   tests/ripstation_check.py <dir>"""
import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

import yaml

d = Path(sys.argv[1])
s = json.loads((Path(__file__).parent / "ripstation_vector.json").read_text(encoding="utf-8"))
user = yaml.safe_load((d / "user-data").read_text(encoding="utf-8"))
net = yaml.safe_load((d / "network-config").read_text(encoding="utf-8"))

assert (d / "user-data").read_text(encoding="utf-8").startswith("#cloud-config\n")
assert net["network"]["wifis"]["wlan0"]["access-points"][s["wifi_ssid"]]["password"] == s["wifi_password"]
assert net["network"]["wifis"]["wlan0"]["regulatory-domain"] == "DE"
assert user["users"][0]["ssh_authorized_keys"] == [s["ssh_key"]]
assert user["enable_ssh"] is True
conf = user["write_files"][0]["content"]
assert "NAS_PASSWORD=" + s["nas_password"].replace("\n", " ") + "\n" in conf, conf
assert "NAS_URL=//nas/Meine Musik\n" in conf

# setup-station.py reads what the page wrote
sys.path.insert(0, str(Path(__file__).parent.parent / "ripstation"))
import importlib.machinery, importlib.util  # noqa: E401
loader = importlib.machinery.SourceFileLoader("st", str(Path(__file__).parent.parent / "ripstation" / "setup-station.py"))
st = importlib.util.module_from_spec(importlib.util.spec_from_loader("st", loader))
loader.exec_module(st)
with tempfile.NamedTemporaryFile("w", suffix=".conf", encoding="utf-8", delete=False) as f:
    f.write(conf)
read = st.read_conf(f.name)
assert read["NAS_PASSWORD"] == s["nas_password"].replace("\n", " ") and read["WEB_PASSWORD"] == "geheim", read
assert st.mount_entry(read)[0].startswith("//nas/Meine\\040Musik /mnt/nas cifs ")

# cloud-init's schema, without the keys only Raspberry Pi OS's cloud-init knows
if shutil.which("cloud-init"):
    for key in ("enable_ssh", "rpi"):
        user.pop(key, None)
    with tempfile.TemporaryDirectory() as tmp:
        ud = Path(tmp) / "user-data"
        ud.write_text("#cloud-config\n" + yaml.safe_dump(user, allow_unicode=True), encoding="utf-8")
        for path, kind in ((ud, "cloud-config"), (d / "network-config", "network-config")):
            r = subprocess.run(["cloud-init", "schema", "-c", str(path), "--schema-type", kind], capture_output=True, text=True)
            assert r.returncode == 0 and "Valid schema" in r.stdout, (kind, r.stdout, r.stderr)
    print("ripstation files ok (cloud-init schema checked)")
else:
    print("ripstation files ok (cloud-init not installed: schema not checked)")
