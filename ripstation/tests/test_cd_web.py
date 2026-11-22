import base64
import fcntl
import importlib.machinery
import importlib.util
import json
import os
import re
import shutil
import subprocess
import tempfile
import threading
import unittest
import urllib.error
import urllib.request
from http.server import ThreadingHTTPServer
from pathlib import Path

TMP = Path(tempfile.mkdtemp())
os.environ["CDRIP_RUN_DIR"] = str(TMP / "run")
ROOT = Path(__file__).resolve().parent.parent
loader = importlib.machinery.SourceFileLoader("cd_web", str(ROOT / "bin" / "cd-web"))
spec = importlib.util.spec_from_loader("cd_web", loader)
web = importlib.util.module_from_spec(spec)
loader.exec_module(web)

JOB = "20261108-125809-830b440a"


class Base(unittest.TestCase):
    def setUp(self):
        shutil.rmtree(TMP, ignore_errors=True)
        (TMP / "run").mkdir(parents=True)
        self.hold_lock()  # a "ripping" status belongs to a live cd-rip
        self.state = TMP / "state"
        (self.state / "jobs" / JOB).mkdir(parents=True)
        (self.state / "jobs" / JOB / "meta.json").write_text(json.dumps(
            {"artist": "Star Wars <b>", "album": "Kampf", "cls": "KIDS"}), encoding="utf-8")
        (self.state / "done.tsv").write_text(
            "2026-11-08 10:00:00\tm\tc\tMUSIC\tMarillion\tBrave\t/x\n", encoding="utf-8")
        self.cfg = dict(web.DEFAULTS, STATE_DIR=str(self.state), MOUNT_POINTS="")
        self.mounts = web.Mounts("")

    def hold_lock(self):
        """Like cd-rip: an flock on RUN_DIR/lock, here held by this test."""
        self.lock = open(TMP / "run" / "lock", "w")
        fcntl.flock(self.lock, fcntl.LOCK_EX)
        self.addCleanup(self.lock.close)

    def set_state(self, state):
        (TMP / "run" / "status.json").write_text(json.dumps({"state": state}), encoding="utf-8")


class Cancel(Base):
    def test_a_dead_rip_shows_as_idle(self):
        self.set_state("ripping")
        self.assertEqual(web.rip_pid(), os.getpid())
        self.lock.close()  # the cd-rip is gone (crash, power cut, kill -9)
        self.assertIsNone(web.rip_pid())
        self.assertEqual(web.build_status(self.cfg, self.mounts)["status"], {"state": "idle", "message": "Abgebrochen"})
        self.set_state("delivering")  # only a read or rip is second-guessed
        self.assertEqual(web.current_status()["state"], "delivering")

    def test_cancel_signals_the_rip(self):
        self.lock.close()
        p = subprocess.Popen(["python3", "-I", "-c", "import fcntl,sys,time; f=open(sys.argv[1],'w'); fcntl.flock(f, fcntl.LOCK_EX); "
                              "print(flush=True); time.sleep(30)", str(TMP / "run" / "lock")], stdout=subprocess.PIPE)
        try:
            p.stdout.readline()  # the lock is held
            self.set_state("ripping")
            self.assertEqual(web.rip_pid(), p.pid)
            self.assertEqual(web.handle_post("/api/cancel", {}, self.cfg)[0], 200)
            self.assertEqual(p.wait(5), -15)
        finally:
            p.kill()
        self.set_state("idle")
        self.assertEqual(web.handle_post("/api/cancel", {}, self.cfg)[0], 409)


class Logic(Base):
    def test_status_shape(self):
        d = web.build_status(self.cfg, self.mounts)
        self.assertEqual(d["status"]["state"], "idle")
        self.assertEqual(d["jobs"][0]["job"], JOB)
        self.assertEqual(d["jobs"][0]["cls"], "Kinderhörspiel")
        self.assertEqual(d["history"][0]["artist"], "Marillion")

    def test_running_rip_is_not_a_failed_job(self):
        running = self.state / "jobs" / "20261108-160443-df0de20f"
        running.mkdir()
        self.assertEqual([j["failed"] for j in web.list_jobs(self.cfg)], [False, True])
        self.assertEqual([j["job"] for j in web.list_jobs(self.cfg, busy=True)], [JOB])

    def test_busy_blocks_actions(self):
        self.set_state("ripping")
        for path, data in (("/api/eject", {}), ("/api/flush", {}),
                           ("/api/relabel", {"job": JOB, "release": "e6aa0db3-eda9-4874-8ba5-5fe76491e1c7"})):
            self.assertEqual(web.handle_post(path, data, self.cfg)[0], 409, path)

    def test_relabel_validation(self):
        self.assertEqual(web.handle_post("/api/relabel", {"job": "../../x", "release": "x"}, self.cfg)[0], 400)
        self.assertEqual(web.handle_post("/api/relabel", {"job": JOB, "release": "nonsense"}, self.cfg)[0], 400)
        self.assertEqual(web.handle_post("/api/relabel", {"job": "20250101-000000-deadbeef",
                         "release": "e6aa0db3-eda9-4874-8ba5-5fe76491e1c7"}, self.cfg)[0], 400)

    def test_relabel_passes_uuid_from_url_to_cd_rip(self):
        calls = []
        orig = web.run_cmd
        web.run_cmd = lambda cmd, timeout: (calls.append(cmd), (0, ["ok"]))[1]
        try:
            code, _ = web.handle_post("/api/relabel", {"job": JOB, "release":
                                      "https://musicbrainz.org/release/E6AA0DB3-EDA9-4874-8BA5-5FE76491E1C7"}, self.cfg)
        finally:
            web.run_cmd = orig
        self.assertEqual(code, 200)
        self.assertEqual(calls[0][-1], "e6aa0db3-eda9-4874-8ba5-5fe76491e1c7")
        self.assertIn("--relabel", calls[0])


class Contract(Base):
    """The Flüstertüte (main/ripper.c) reads these names; changing them breaks its screen."""

    def test_status_names(self):
        (TMP / "run" / "status.json").write_text(json.dumps(
            {"state": "ripping", "progress": 0.5, "artist": "A", "album": "B", "cls": "Musik", "message": "m",
             "detect": {"choices": ["MUSIC"], "source": "s", "reason": "r", "year": "", "dests": [], "hint": ""}}),
            encoding="utf-8")
        d = web.build_status(self.cfg, self.mounts)
        for key in ("status", "classes", "correction", "force", "jobs", "history", "attach", "mounts", "now"):
            self.assertIn(key, d)
        self.assertEqual(set(d["classes"]), {"KIDS", "SPOKEN", "MUSIC", "UNSORTED"})
        self.assertEqual(d["classes"]["MUSIC"], "Musik")  # status.cls is a label, classes maps key -> label
        self.assertIn("failed", d["jobs"][0])
        self.assertEqual(d["status"]["detect"]["choices"], ["MUSIC"])

    def test_actions_and_bodies(self):
        self.set_state("ripping")
        (TMP / "run" / "status.json").write_text(json.dumps(
            {"state": "ripping", "cls": "Musik", "detect": {"choices": ["MUSIC", "KIDS"]}}), encoding="utf-8")
        self.assertEqual(web.handle_post("/api/correct", {"cls": "KIDS", "release": ""}, self.cfg)[0], 200)
        self.assertEqual(web.build_status(self.cfg, self.mounts)["correction"], {"cls": "KIDS"})
        self.assertEqual(web.handle_post("/api/correct", {"cls": None, "release": ""}, self.cfg)[0], 200)
        self.assertEqual(web.build_status(self.cfg, self.mounts)["correction"], {})
        self.assertEqual(web.handle_post("/api/force", {"on": True}, self.cfg)[0], 200)


class Force(Base):
    def test_toggle(self):
        web.handle_post("/api/force", {"on": True}, self.cfg)
        self.assertTrue(web.build_status(self.cfg, self.mounts)["force"])
        web.handle_post("/api/force", {"on": False}, self.cfg)
        self.assertFalse(web.build_status(self.cfg, self.mounts)["force"])


class Correct(Base):
    def ripping(self, cls="Kinderhörspiel", choices=("KIDS", "SPOKEN")):
        (TMP / "run" / "status.json").write_text(json.dumps(
            {"state": "ripping", "cls": cls, "detect": {"choices": list(choices)}}), encoding="utf-8")

    def test_class_and_release(self):
        self.ripping()
        mbid = "b6f8e0c2-1111-4222-8333-444455556666"
        self.assertEqual(web.handle_post("/api/correct", {"cls": "SPOKEN", "release": f"https://musicbrainz.org/release/{mbid}"},
                                         self.cfg)[0], 200)
        self.assertEqual(json.loads((TMP / "run" / "correction").read_text()), {"cls": "SPOKEN", "release": mbid})
        self.assertEqual(web.build_status(self.cfg, self.mounts)["correction"]["cls"], "SPOKEN")

    def test_same_class_clears(self):
        self.ripping()
        web.handle_post("/api/correct", {"cls": "SPOKEN"}, self.cfg)
        web.handle_post("/api/correct", {"cls": "KIDS"}, self.cfg)  # back to the detected class
        self.assertFalse((TMP / "run" / "correction").exists())

    def test_rejects(self):
        self.ripping()
        self.assertEqual(web.handle_post("/api/correct", {"cls": "MUSIC"}, self.cfg)[0], 400)   # needs FLAC, not ripped
        self.assertEqual(web.handle_post("/api/correct", {"cls": "NOPE"}, self.cfg)[0], 400)
        self.assertEqual(web.handle_post("/api/correct", {"release": "kein link"}, self.cfg)[0], 400)
        self.set_state("idle")
        self.assertEqual(web.handle_post("/api/correct", {"cls": "SPOKEN"}, self.cfg)[0], 409)


class Http(Base):
    def start(self, password=""):
        handler = type("H", (web.Handler,), {"cfg": dict(self.cfg, WEB_PASSWORD=password), "mounts": self.mounts})
        self.srv = ThreadingHTTPServer(("127.0.0.1", 0), handler)
        threading.Thread(target=self.srv.serve_forever, daemon=True).start()
        self.addCleanup(self.srv.server_close)
        self.addCleanup(self.srv.shutdown)
        return f"http://127.0.0.1:{self.srv.server_address[1]}"

    def req(self, url, data=None, headers=None):
        r = urllib.request.Request(url, data=json.dumps(data).encode() if data is not None else None,
                                   headers=headers or {}, method="POST" if data is not None else "GET")
        try:
            with urllib.request.urlopen(r, timeout=5) as resp:
                return resp.status, resp.read()
        except urllib.error.HTTPError as e:
            return e.code, e.read()

    def test_page_and_api(self):
        base = self.start()
        code, body = self.req(base + "/")
        self.assertEqual(code, 200)
        self.assertIn(b"CD-Ripper", body)
        code, body = self.req(base + "/api/status")
        self.assertEqual(json.loads(body)["status"]["state"], "idle")
        self.assertEqual(self.req(base + "/nope")[0], 404)

    def test_page_language(self):
        base = self.start()
        self.assertIn(b'<html lang="de">', self.req(base + "/")[1])
        self.cfg["LANG"] = "en"
        base = self.start()
        self.assertIn(b'<html lang="en">', self.req(base + "/")[1])

    def test_bt_requests_go_to_cd_bt(self):
        from http.server import BaseHTTPRequestHandler
        seen = []

        class FakeBt(BaseHTTPRequestHandler):
            def log_message(self, *a):
                pass

            def _answer(self, code, body):
                raw = json.dumps(body).encode()
                self.send_response(code)
                self.send_header("Content-Length", str(len(raw)))
                self.end_headers()
                self.wfile.write(raw)

            def do_GET(self):
                seen.append(("GET", self.path, None, self.headers.get("Authorization")))
                self._answer(200, {"speaker": {"name": "Boombox"}})

            def do_POST(self):
                body = self.rfile.read(int(self.headers["Content-Length"]))
                seen.append(("POST", self.path, body, self.headers.get("X-Requested-With")))
                self._answer(502, {"error": "x", "code": "pair_failed"})

        bt_srv = ThreadingHTTPServer(("127.0.0.1", 0), FakeBt)
        threading.Thread(target=bt_srv.serve_forever, daemon=True).start()
        self.addCleanup(bt_srv.server_close)
        self.addCleanup(bt_srv.shutdown)
        self.cfg["BT_PORT"] = str(bt_srv.server_address[1])
        auth = {"Authorization": "Basic " + base64.b64encode(b"cd:pw").decode()}
        base = self.start(password="pw")

        code, body = self.req(base + "/api/bt/status", headers=auth)
        self.assertEqual((code, json.loads(body)["speaker"]["name"]), (200, "Boombox"))
        self.assertEqual(seen[-1][3], auth["Authorization"])  # cd-bt checks the same login itself
        code, body = self.req(base + "/api/bt/pair", {"address": "AA:BB:CC:DD:EE:FF"},
                              headers=dict(auth, **{"X-Requested-With": "cd-web", "Content-Type": "application/json"}))
        self.assertEqual((code, json.loads(body)["code"]), (502, "pair_failed"))
        self.assertEqual(seen[-1][:3], ("POST", "/api/bt/pair", b'{"address": "AA:BB:CC:DD:EE:FF"}'))
        self.assertEqual(self.req(base + "/api/bt/status")[0], 401)  # no login: refused before forwarding

    def test_icons(self):
        base = self.start()
        code, body = self.req(base + "/icon.svg")
        self.assertEqual((code, body[:4]), (200, b"<svg"))
        code, body = self.req(base + "/apple-touch-icon.png")
        self.assertEqual((code, body[:8]), (200, b"\x89PNG\r\n\x1a\n"))
        self.assertIn(b'rel="icon"', self.req(base + "/")[1])

    def test_post_needs_csrf_header(self):
        base = self.start()
        self.assertEqual(self.req(base + "/api/force", {"on": True})[0], 403)
        self.assertEqual(self.req(base + "/api/force", {"on": True}, {"X-Requested-With": "cd-web"})[0], 200)

    def test_basic_auth(self):
        base = self.start("geheim")
        self.assertEqual(self.req(base + "/api/status")[0], 401)
        good = {"Authorization": "Basic " + base64.b64encode(b"cd:geheim").decode()}
        bad = {"Authorization": "Basic " + base64.b64encode(b"cd:falsch").decode()}
        self.assertEqual(self.req(base + "/api/status", headers=good)[0], 200)
        self.assertEqual(self.req(base + "/api/status", headers=bad)[0], 401)


class Page(unittest.TestCase):
    def test_javascript_syntax_and_no_innerhtml(self):
        script = re.search(r"<script>(.*)</script>", web.PAGE, re.S).group(1)
        self.assertNotIn("innerHTML", script)  # all dynamic text must go through textContent
        if shutil.which("node"):
            js = TMP / "page.js"
            TMP.mkdir(parents=True, exist_ok=True)
            js.write_text(script, encoding="utf-8")
            r = subprocess.run(["node", "--check", str(js)], capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, r.stderr)


if __name__ == "__main__":
    unittest.main()
