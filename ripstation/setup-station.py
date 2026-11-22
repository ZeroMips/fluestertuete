#!/usr/bin/env python3
"""Turn /etc/cd-ripper/station.conf (written by the setup website) into the station's configuration:
cd-ripper.conf, the NAS mount in /etc/fstab and its credentials. Run by install.sh as root.

    setup-station.py [--root DIR]      (--root: write below DIR instead of /, for tests)

station.conf, KEY=value (no shell syntax):
    LANG=de|en
    NAS_TYPE=smb|nfs|webdav
    NAS_URL=//server/share | server:/export | https://server/path
    NAS_USER=, NAS_PASSWORD=          (smb, webdav)
    NAS_OPTIONS=                      (optional extra mount options)
    DIR_KIDS=Hörspiele  DIR_SPOKEN=Hörbücher  DIR_MUSIC=Musik  DIR_UNSORTED=Unsortiert   (inside the share)
    GNUDB_CONTACT=                    (e-mail; empty = GnuDB off)
    WEB_PASSWORD=                     (login for the station's web page and the player; empty = none)
"""
import argparse
import os
import re
import subprocess
import sys
from pathlib import Path

MOUNT = "/mnt/nas"
SYSTEMD_OPTS = "_netdev,nofail,x-systemd.automount,x-systemd.idle-timeout=600,x-systemd.mount-timeout=30"
OWNER = "uid=ripper,gid=ripper,file_mode=0664,dir_mode=0775"
BEGIN, END = "# ripstation begin", "# ripstation end"
DIR_DEFAULTS = {"KIDS": "Hörspiele", "SPOKEN": "Hörbücher", "MUSIC": "Musik", "UNSORTED": "Unsortiert"}


def read_conf(path):
    conf = {}
    for line in Path(path).read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        value = value.strip()
        if len(value) >= 2 and value[0] == value[-1] and value[0] in "\"'":
            value = value[1:-1]
        conf[key.strip()] = value
    return conf


def fstab_escape(s):
    """fstab fields are separated by blanks: octal escapes for space, tab, backslash."""
    return s.replace("\\", "\\134").replace(" ", "\\040").replace("\t", "\\011")


def clean_dir(d, default):
    parts = [p for p in (d or default).replace("\\", "/").split("/") if p not in ("", ".", "..")]
    return "/".join(parts) or default


def mount_entry(conf):
    """(fstab line, {path: (content, mode)} of secret files, packages) for the NAS share."""
    kind = conf.get("NAS_TYPE", "").lower()
    url = conf.get("NAS_URL", "").strip()
    user, password = conf.get("NAS_USER", ""), conf.get("NAS_PASSWORD", "")
    extra = conf.get("NAS_OPTIONS", "").strip().strip(",")
    if not url:
        raise ValueError("NAS_URL is empty")
    if kind == "smb":
        url = url.replace("\\", "/")
        if url.lower().startswith("smb:"):
            url = url[4:]
        url = "//" + url.lstrip("/")
        if not re.match(r"^//[^/]+/[^/].*", url):
            raise ValueError("NAS_URL for SMB must look like //server/share")
        secrets = {"/etc/cd-ripper/smb-credentials": (f"username={user}\npassword={password}\n", 0o600)}
        opts = f"credentials=/etc/cd-ripper/smb-credentials,{OWNER},iocharset=utf8,{SYSTEMD_OPTS}"
        return f"{fstab_escape(url.rstrip('/'))} {MOUNT} cifs {opts + (',' + extra if extra else '')} 0 0", secrets, ["cifs-utils"]
    if kind == "nfs":
        if ":" not in url:
            raise ValueError("NAS_URL for NFS must look like server:/export")
        opts = extra or "vers=4.1"
        return f"{fstab_escape(url)} {MOUNT} nfs {opts},{SYSTEMD_OPTS} 0 0", {}, ["nfs-common"]
    if kind == "webdav":
        if not re.match(r"^https?://", url):
            raise ValueError("NAS_URL for WebDAV must start with https:// or http://")
        q = lambda s: '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'
        secrets = {"/etc/davfs2/secrets": (f"{MOUNT} {q(user)} {q(password)}\n", 0o600)}
        opts = f"{OWNER},{SYSTEMD_OPTS}"
        return f"{fstab_escape(url)} {MOUNT} davfs {opts + (',' + extra if extra else '')} 0 0", secrets, ["davfs2"]
    raise ValueError(f"NAS_TYPE must be smb, nfs or webdav, not {kind!r}")


def ripper_conf(conf):
    """The managed part of cd-ripper.conf."""
    dirs = {c: clean_dir(conf.get(f"DIR_{c}"), d) for c, d in DIR_DEFAULTS.items()}
    lines = [
        "# Written by setup-station.py from station.conf: change station.conf and run",
        "#   sudo /usr/local/lib/ripstation/setup-station.py",
        "# Everything cd-rip, cd-web and cd-bt understand is described in /usr/local/lib/ripstation/cd-ripper.conf.",
        f"LANG={conf.get('LANG', 'de')}",
        f"GNUDB_CONTACT={conf.get('GNUDB_CONTACT', '')}",
        f"MOUNT_POINTS={MOUNT}",
        f"LIBRARY_ROOTS={MOUNT}",
    ]
    lines += [f"DEST_{c}_MP3={MOUNT}/{d}" for c, d in dirs.items()]
    lines += [f"BT_SHARE_DIR={MOUNT}/{dirs['KIDS']}", f"WEB_PASSWORD={conf.get('WEB_PASSWORD', '')}"]
    for key in ("DISCOGS_TOKEN", "BT_VOLUME", "WEB_PORT", "BT_PORT"):  # passed through when set
        if conf.get(key):
            lines.append(f"{key}={conf[key]}")
    return "\n".join(lines) + "\n"


def replace_block(text, block):
    """Replace (or append) the ripstation block in fstab."""
    out, skip = [], False
    for line in text.splitlines():
        if line.strip() == BEGIN:
            skip = True
        elif line.strip() == END:
            skip = False
        elif not skip:
            out.append(line)
    while out and not out[-1].strip():
        out.pop()
    return "\n".join(out + ["", BEGIN, block, END]) + "\n"


def write(root, path, content, mode, owner=None):
    p = Path(root + path)
    p.parent.mkdir(parents=True, exist_ok=True)
    fd = os.open(p, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, mode)
    with os.fdopen(fd, "w", encoding="utf-8") as f:
        f.write(content)
    os.chmod(p, mode)
    if owner and not root:
        import pwd
        try:
            os.chown(p, pwd.getpwnam(owner).pw_uid, 0)
        except KeyError:
            pass  # the user comes later (install.sh creates it first, so only by hand)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default="")
    ap.add_argument("--packages", action="store_true", help="only print the packages the NAS type needs")
    a = ap.parse_args()
    conf = read_conf(a.root + "/etc/cd-ripper/station.conf")
    line, secrets, packages = mount_entry(conf)
    if a.packages:
        print(" ".join(packages))
        return
    # The services run as "ripper" (with group cdrom): the file is theirs, unreadable for everyone else.
    write(a.root, "/etc/cd-ripper/cd-ripper.conf", ripper_conf(conf), 0o640, owner="ripper")
    for path, (content, mode) in secrets.items():
        write(a.root, path, content, mode)
    if conf.get("NAS_TYPE", "").lower() == "webdav":
        # Nextcloud and many others refuse WebDAV locks: davfs2 must not ask for them.
        davconf = Path(a.root + "/etc/davfs2/davfs2.conf")
        text = davconf.read_text(encoding="utf-8") if davconf.exists() else ""
        lines = [ln for ln in text.splitlines() if not re.match(r"^\s*use_locks\b", ln)]
        write(a.root, "/etc/davfs2/davfs2.conf", "\n".join(lines + ["use_locks 0"]) + "\n", 0o644)
    fstab = Path(a.root + "/etc/fstab")
    old = fstab.read_text(encoding="utf-8") if fstab.exists() else ""
    write(a.root, "/etc/fstab", replace_block(old, line), 0o644)
    Path(a.root + MOUNT).mkdir(parents=True, exist_ok=True)
    print(f"NAS ({conf.get('NAS_TYPE')}) at {MOUNT}; stories in {MOUNT}/{clean_dir(conf.get('DIR_KIDS'), 'Hörspiele')}")
    if not a.root:
        apply_now()


def apply_now():
    """On the station itself: make systemd and the services use the new settings right away."""
    run = lambda *cmd: subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if not Path("/run/systemd/system").is_dir():
        return  # not a running systemd (image build, container): the next boot picks it up
    run("systemctl", "daemon-reload")
    run("umount", "-l", MOUNT)  # the old share with the old login
    run("systemctl", "restart", "mnt-nas.automount")
    for unit in ("cd-web.service", "cd-bt.service"):
        if run("systemctl", "is-enabled", "--quiet", unit).returncode == 0:
            run("systemctl", "restart", unit)


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError) as e:
        print(f"setup-station: {e}", file=sys.stderr)
        sys.exit(1)
