import functools
import importlib.machinery
import importlib.util
import os
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
loader = importlib.machinery.SourceFileLoader("cd_bt", str(ROOT / "bin" / "cd-bt"))
spec = importlib.util.spec_from_loader("cd_bt", loader)
bt = importlib.util.module_from_spec(spec)
loader.exec_module(bt)


class NaturalOrder(unittest.TestCase):
    def sort(self, names):
        return sorted(names, key=functools.cmp_to_key(bt.nat_cmp))

    def test_numbers(self):
        self.assertEqual(self.sort(["Folge 10", "Folge 2", "Folge 1"]), ["Folge 1", "Folge 2", "Folge 10"])

    def test_leading_zeros_and_case(self):
        self.assertEqual(self.sort(["b.mp3", "A.mp3", "007.mp3", "7a.mp3"]), ["007.mp3", "7a.mp3", "A.mp3", "b.mp3"])

    def test_umlauts_sort_with_base_letter(self):
        self.assertEqual(self.sort(["Zebra", "Ärger", "Apfel", "Ostern", "Öl"]), ["Apfel", "Ärger", "Öl", "Ostern", "Zebra"])

    def test_nfd_umlaut_equals_nfc(self):
        self.assertEqual(bt.nat_cmp("Fängen", "Fängen") != 0, True)  # tie broken by bytes, but adjacent
        self.assertEqual(self.sort(["Fz", "Fängen", "Fb"]), ["Fängen", "Fb", "Fz"])


class Files(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.share = Path(self.tmp.name) / "share"
        album = self.share / "Hörspiele für Kinder" / "Serie" / "Folge 1"
        album.mkdir(parents=True)
        for n in ["10 - Ende.mp3", "02 - Mitte.mp3", "01 - Anfang.mp3", "cover.jpg", ".hidden.mp3"]:
            (album / n).write_bytes(b"x")
        (album / "@eaDir").mkdir()
        self.album = album

    def tearDown(self):
        self.tmp.cleanup()

    def test_resolve_maps_share_paths(self):
        self.assertEqual(bt.resolve(str(self.share), "/Hörspiele für Kinder/Serie/Folge 1"), os.path.realpath(self.album))

    def test_resolve_finds_albums_below_another_top_folder(self):
        # Player: WebDAV root /music/Kinder; here the stories folder itself is the share dir.
        stories = str(self.share / "Hörspiele für Kinder")
        self.assertEqual(bt.resolve(stories, "/music/Kinder/Serie/Folge 1"), os.path.realpath(self.album))
        self.assertEqual(bt.resolve(stories, "/Serie/Folge 1"), os.path.realpath(self.album))
        self.assertIsNone(bt.resolve(stories, "/music/Kinder/Serie/Folge 2"))

    def test_resolve_refuses_to_leave_the_share(self):
        self.assertIsNone(bt.resolve(str(self.share), "/../.."))
        self.assertIsNone(bt.resolve(str(self.share), "/Hörspiele für Kinder/../../etc"))
        self.assertIsNone(bt.resolve(str(self.share), "/gibt es nicht"))

    def test_tracks_are_sorted_mp3s_only(self):
        self.assertEqual(bt.album_tracks(str(self.album)), ["01 - Anfang.mp3", "02 - Mitte.mp3", "10 - Ende.mp3"])


class Codes(unittest.TestCase):
    def test_messages_have_codes(self):
        self.assertEqual(bt.code_of("Album zu Ende"), "album_end")
        self.assertEqual(bt.code_of("Boombox nicht erreichbar. Ist sie eingeschaltet?"), "speaker_unreachable")
        self.assertEqual(bt.code_of("mpg123 fehlt: No such file"), "mpg123_missing")
        self.assertEqual(bt.code_of("something odd from mpg123"), "player_error")
        self.assertEqual(bt.code_of(""), "")


class Requests(unittest.TestCase):
    class FakePlayer:
        volume = 50

        def __init__(self):
            self.calls = []

        def __getattr__(self, name):
            def call(*a, **k):
                self.calls.append((name, a, k))
                return 200, {}
            return call

    def test_play_needs_an_album(self):
        p = self.FakePlayer()
        self.assertEqual(bt.handle_post(p, "/api/bt/play", {})[0], 400)
        self.assertEqual(bt.handle_post(p, "/api/bt/play", {"album": "/a", "track": "t.mp3", "pos": 12})[0], 200)
        self.assertEqual(p.calls[-1], ("play", ("/a", "t.mp3", 12.0), {}))

    def test_seek_relative_and_absolute(self):
        p = self.FakePlayer()
        bt.handle_post(p, "/api/bt/seek", {"delta": -30})
        bt.handle_post(p, "/api/bt/seek", {"pos": 90})
        self.assertEqual(p.calls, [("seek", (), {"delta": -30.0}), ("seek", (), {"pos": 90.0})])

    def test_unknown_path(self):
        self.assertEqual(bt.handle_post(self.FakePlayer(), "/api/bt/nope", {})[0], 404)


class Pairing(unittest.TestCase):
    """bluetoothctl replaced by canned answers."""
    DEVICES = ("Device D8:86:0B:00:00:01 Boombox\n"
               "Device 11:22:33:44:55:66 11-22-33-44-55-66\n"
               "Device AA:BB:CC:DD:EE:FF Phone\n")
    INFO = {"D8:86:0B:00:00:01": "Name: Boombox\nIcon: audio-card\nPaired: no\n",
            "11:22:33:44:55:66": "Paired: no\n",
            "AA:BB:CC:DD:EE:FF": "Name: Phone\nIcon: phone\nPaired: yes\n"}

    def setUp(self):
        self.orig = bt.bt
        self.calls = []

        def fake(*args, timeout=15):
            self.calls.append(args)
            if args[0] == "devices":
                return self.DEVICES
            if args[0] == "info":
                return self.INFO.get(args[1], "")
            return ""
        bt.bt = fake

    def tearDown(self):
        bt.bt = self.orig

    def test_parse_devices_drops_address_names(self):
        self.assertEqual(bt.parse_devices(self.DEVICES),
                         [("D8:86:0B:00:00:01", "Boombox"), ("11:22:33:44:55:66", ""), ("AA:BB:CC:DD:EE:FF", "Phone")])

    def test_scan_lists_speakers_first_and_drops_nameless_beacons(self):
        found = bt.scan(seconds=1)
        self.assertEqual([d["address"] for d in found], ["D8:86:0B:00:00:01", "AA:BB:CC:DD:EE:FF"])
        self.assertTrue(found[0]["audio"])
        self.assertTrue(found[1]["paired"])
        self.assertEqual(self.calls[0], ("--timeout", "1", "scan", "on"))

    def test_pair_reports_failure(self):
        p = bt.Player(dict(bt.DEFAULTS))
        code, body = p.pair("d8:86:0b:00:00:01")
        self.assertEqual(code, 502)
        self.assertEqual(bt.code_of(body["error"]), "pair_failed")

    def test_pair_rejects_bad_address(self):
        with self.assertRaises(ValueError):
            bt.Player(dict(bt.DEFAULTS)).pair("not a mac")


if __name__ == "__main__":
    unittest.main()
