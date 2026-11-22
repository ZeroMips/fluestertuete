import importlib.machinery
import importlib.util
import os
import stat
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SCRIPT = ROOT / "setup-station.py"
loader = importlib.machinery.SourceFileLoader("setup_station", str(SCRIPT))
spec = importlib.util.spec_from_loader("setup_station", loader)
st = importlib.util.module_from_spec(spec)
loader.exec_module(st)


class MountEntry(unittest.TestCase):
    def test_smb(self):
        line, secrets, pkgs = st.mount_entry({"NAS_TYPE": "smb", "NAS_URL": "\\\\nas\\Meine Musik\\",
                                              "NAS_USER": "kim", "NAS_PASSWORD": "p=w ö"})
        self.assertTrue(line.startswith("//nas/Meine\\040Musik /mnt/nas cifs credentials=/etc/cd-ripper/smb-credentials,"))
        self.assertIn("x-systemd.automount", line)
        self.assertEqual(secrets["/etc/cd-ripper/smb-credentials"], ("username=kim\npassword=p=w ö\n", 0o600))
        self.assertEqual(pkgs, ["cifs-utils"])

    def test_smb_url_forms(self):
        for url in ("//nas/share", "smb://nas/share", "nas/share"):
            self.assertTrue(st.mount_entry({"NAS_TYPE": "smb", "NAS_URL": url})[0].startswith("//nas/share "), url)
        with self.assertRaises(ValueError):
            st.mount_entry({"NAS_TYPE": "smb", "NAS_URL": "//nas"})

    def test_nfs(self):
        line, secrets, pkgs = st.mount_entry({"NAS_TYPE": "nfs", "NAS_URL": "nas:/volume1/music"})
        self.assertEqual(line.split()[:3], ["nas:/volume1/music", "/mnt/nas", "nfs"])
        self.assertTrue(line.split()[3].startswith("vers=4.1,_netdev"))
        self.assertEqual(secrets, {})
        line = st.mount_entry({"NAS_TYPE": "nfs", "NAS_URL": "nas:/x", "NAS_OPTIONS": "vers=3"})[0]
        self.assertTrue(line.split()[3].startswith("vers=3,_netdev"))

    def test_webdav_quotes_the_secret(self):
        line, secrets, pkgs = st.mount_entry({"NAS_TYPE": "webdav", "NAS_URL": "https://cloud.example.org/remote.php/dav/files/kim",
                                              "NAS_USER": "kim", "NAS_PASSWORD": 'a"b\\c'})
        self.assertEqual(line.split()[2], "davfs")
        self.assertEqual(secrets["/etc/davfs2/secrets"][0], '/mnt/nas "kim" "a\\"b\\\\c"\n')
        with self.assertRaises(ValueError):
            st.mount_entry({"NAS_TYPE": "webdav", "NAS_URL": "cloud.example.org"})

    def test_unknown_type(self):
        with self.assertRaises(ValueError):
            st.mount_entry({"NAS_TYPE": "ftp", "NAS_URL": "x"})


class RipperConf(unittest.TestCase):
    def test_folders_inside_the_share(self):
        conf = st.ripper_conf({"DIR_KIDS": "/Kinder/Hörspiele/", "DIR_MUSIC": "../../etc", "LANG": "en",
                               "GNUDB_CONTACT": "a@b.c", "WEB_PASSWORD": "pw"})
        self.assertIn("DEST_KIDS_MP3=/mnt/nas/Kinder/Hörspiele\n", conf)
        self.assertIn("DEST_MUSIC_MP3=/mnt/nas/etc\n", conf)  # ".." cannot climb out of the share
        self.assertIn("DEST_SPOKEN_MP3=/mnt/nas/Hörbücher\n", conf)
        self.assertIn("BT_SHARE_DIR=/mnt/nas/Kinder/Hörspiele\n", conf)
        self.assertIn("LANG=en\n", conf)
        self.assertIn("WEB_PASSWORD=pw\n", conf)


class WholeRun(unittest.TestCase):
    def run_script(self, root, *args):
        return subprocess.run([sys.executable, "-I", str(SCRIPT), "--root", root, *args], capture_output=True, text=True)

    def test_writes_files_and_replaces_its_fstab_block(self):
        with tempfile.TemporaryDirectory() as root:
            os.makedirs(root + "/etc/cd-ripper")
            Path(root + "/etc/fstab").write_text("proc /proc proc defaults 0 0\n", encoding="utf-8")
            conf = Path(root + "/etc/cd-ripper/station.conf")
            conf.write_text("NAS_TYPE=smb\nNAS_URL=//nas/music\nNAS_USER=kim\nNAS_PASSWORD=geheim\n", encoding="utf-8")
            r = self.run_script(root)
            self.assertEqual(r.returncode, 0, r.stderr)
            cred = Path(root + "/etc/cd-ripper/smb-credentials")
            self.assertEqual(stat.S_IMODE(cred.stat().st_mode), 0o600)
            self.assertIn("DEST_KIDS_MP3=/mnt/nas/Hörspiele", Path(root + "/etc/cd-ripper/cd-ripper.conf").read_text(encoding="utf-8"))
            self.assertTrue(Path(root + "/mnt/nas").is_dir())

            # Again with WebDAV: one block in fstab, the other lines kept.
            conf.write_text("NAS_TYPE=webdav\nNAS_URL=https://nas/dav\nNAS_USER=kim\nNAS_PASSWORD=x\n", encoding="utf-8")
            Path(root + "/etc/davfs2").mkdir()
            Path(root + "/etc/davfs2/davfs2.conf").write_text("# use_locks 1\nuse_locks 1\ncache_size 50\n", encoding="utf-8")
            self.assertEqual(self.run_script(root).returncode, 0)
            fstab = Path(root + "/etc/fstab").read_text(encoding="utf-8")
            self.assertEqual(fstab.count(st.BEGIN), 1)
            self.assertIn("proc /proc proc", fstab)
            self.assertIn("https://nas/dav /mnt/nas davfs", fstab)
            self.assertNotIn("cifs", fstab)
            dav = Path(root + "/etc/davfs2/davfs2.conf").read_text(encoding="utf-8")
            self.assertEqual(dav, "# use_locks 1\ncache_size 50\nuse_locks 0\n")

    def test_packages_and_errors(self):
        with tempfile.TemporaryDirectory() as root:
            os.makedirs(root + "/etc/cd-ripper")
            Path(root + "/etc/cd-ripper/station.conf").write_text("NAS_TYPE=nfs\nNAS_URL=nas:/x\n", encoding="utf-8")
            self.assertEqual(self.run_script(root, "--packages").stdout.strip(), "nfs-common")
            Path(root + "/etc/cd-ripper/station.conf").write_text("NAS_TYPE=nfs\n", encoding="utf-8")
            r = self.run_script(root)
            self.assertEqual(r.returncode, 1)
            self.assertIn("NAS_URL", r.stderr)


if __name__ == "__main__":
    unittest.main()
