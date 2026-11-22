import importlib.machinery
import importlib.util
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
loader = importlib.machinery.SourceFileLoader("cd_rip", str(ROOT / "bin" / "cd-rip"))
spec = importlib.util.spec_from_loader("cd_rip", loader)
cd = importlib.util.module_from_spec(spec)
loader.exec_module(cd)

DISC = cd.Disc("mbdiscid", "1 3 1000 150 400 700", 1, 3, "a10b0c03")

MB_RELEASE = {
    "id": "rel-1", "title": "Folge 1: Hexenzauber", "date": "2012-05-04",
    "artist-credit": [{"name": "Bibi Blocksberg", "joinphrase": ""}],
    "label-info": [{"label": {"name": "Kiddinx"}}],
    "release-group": {"primary-type": "Other", "secondary-types": ["Audio drama"]},
    "cover-art-archive": {"front": True},
    "media": [{"position": 2, "discs": [{"id": "mbdiscid"}], "tracks": [
        {"title": "Intro", "artist-credit": [{"name": "Bibi Blocksberg"}]},
        {"title": "Zauberei", "artist-credit": [{"name": "Bibi Blocksberg"}]},
        {"title": "Ende", "artist-credit": [{"name": "Bibi Blocksberg"}]}]}],
}


def info(**kw):
    base = cd.parse_release(MB_RELEASE, "mbdiscid", 3, media_total=2)
    base.update(kw)
    return base


class SeriesList(unittest.TestCase):
    def setUp(self):
        self.d = tempfile.mkdtemp()
        p = Path(self.d) / "s.txt"
        p.write_text("# c\nBenjamin Blümchen\nartist: Conni\nlabel: Kiddinx   # inline\nDie drei ???\n", encoding="utf-8")
        self.rules = cd.load_series(p)

    def tearDown(self):
        shutil.rmtree(self.d)

    def test_any_field_case_insensitive(self):
        self.assertTrue(cd.match_series(self.rules, info(artist="BENJAMIN BLÜMCHEN", labels=[])))
        self.assertTrue(cd.match_series(self.rules, info(artist="X", album="Benjamin Blümchen als Koch", labels=[])))

    def test_field_scoped(self):
        self.assertTrue(cd.match_series(self.rules, info(artist="Conni", album="a", labels=[])))
        self.assertIsNone(cd.match_series(self.rules, info(artist="X", album="Conni", labels=[])))
        self.assertTrue(cd.match_series(self.rules, info(artist="X", album="a", labels=["Kiddinx"])))

    def test_whole_word_only(self):
        self.assertIsNone(cd.match_series(self.rules, info(artist="Connie Francis", album="a", labels=[])))

    def test_special_characters(self):
        self.assertTrue(cd.match_series(self.rules, info(artist="Die drei ???", album="a", labels=[])))


class Classify(unittest.TestCase):
    rules = [("any", __import__("re").compile(r"(?<!\w)bibi blocksberg(?!\w)"), "Bibi Blocksberg")]

    def test_button_wins(self):
        self.assertEqual(cd.classify(None, True, [])[0], "KIDS")

    def test_manual_override_class(self):
        self.assertEqual(cd.classify(info(), "MUSIC", self.rules)[0], "MUSIC")
        self.assertEqual(cd.classify(None, "SPOKEN", [])[0], "SPOKEN")

    def test_unknown_goes_unsorted(self):
        self.assertEqual(cd.classify(None, False, [])[0], "UNSORTED")

    def test_series_hit(self):
        self.assertEqual(cd.classify(info(), False, self.rules)[0], "KIDS")

    def test_audio_drama_without_signal_is_unsorted(self):
        i = info(artist="Sherlock", album="Fall 1", labels=["Lübbe"])
        self.assertEqual(cd.classify(i, False, self.rules)[0], "UNSORTED")

    def test_audiobook_goes_spoken(self):
        i = info(artist="Rufus Beck", album="Harry", labels=[], types=["audiobook"])
        self.assertEqual(cd.classify(i, False, self.rules)[0], "SPOKEN")

    def test_plain_music(self):
        i = info(artist="Marillion", album="Brave", labels=["EMI"], types=["album"])
        self.assertEqual(cd.classify(i, False, self.rules)[0], "MUSIC")


class Detection(unittest.TestCase):
    CFG = {"DEST_MUSIC_MP3": "/m", "DEST_MUSIC_FLAC": "/f", "DEST_KIDS_MP3": "/k", "DEST_SPOKEN_MP3": "/s",
           "DEST_UNSORTED_MP3": "/u", "DEST_UNSORTED_FLAC": "/uf"}

    def test_mp3_only_rip_cannot_become_music(self):
        d = cd.detection({"match": "gnudb", "year": "2011"}, "KIDS", "Serienliste: X", ["mp3"], self.CFG)
        self.assertEqual(d["choices"], ["KIDS", "SPOKEN"])
        self.assertEqual((d["source"], d["year"], d["dests"]), ("GnuDB", "2011", [{"fmt": "mp3", "path": "/k"}]))

    def test_full_rip_allows_all(self):
        d = cd.detection(None, "UNSORTED", "keine Metadaten", ["mp3", "flac"], self.CFG)
        self.assertEqual(d["choices"], ["MUSIC", "KIDS", "SPOKEN", "UNSORTED"])
        self.assertEqual(d["source"], "nichts gefunden")

    def test_take_correction_is_consumed(self):
        run = Path(tempfile.mkdtemp())
        (run / "correction").write_text('{"cls": "KIDS"}')
        self.assertEqual(cd.take_correction(run), {"cls": "KIDS"})
        self.assertEqual(cd.take_correction(run), {})
        (run / "correction").write_text("garbage")
        self.assertEqual(cd.take_correction(run), {})


class Library(unittest.TestCase):
    def setUp(self):
        self.root = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.root, True)

    def album(self, rel, n):
        d = self.root / rel
        d.mkdir(parents=True)
        for i in range(n):
            (d / f"{i + 1:02d} - x.mp3").write_bytes(b"x")
        (d / "cover.jpg").write_bytes(b"x")
        return str(d)

    def test_normalising(self):
        self.assertTrue(cd.names_match("04: Kampf der Droiden", "04 Kampf der Droiden"))
        self.assertTrue(cd.names_match("Sigur Rós", "Sigur Ros"))
        self.assertTrue(cd.names_match("Brave (Disc 1)", "2005 - Brave"))
        self.assertTrue(cd.names_match("The Alan Parsons Project", "Alan Parsons Project"))
        self.assertFalse(cd.names_match("Gordon", "Stunt"))

    def test_found_with_same_track_count(self):
        path = self.album("Barenaked Ladies/Gordon", 15)
        found = cd.find_in_library([str(self.root)], "Barenaked Ladies", "Gordon", 15)
        self.assertEqual(found, {"path": path, "tracks": 15, "same": True})

    def test_other_edition_is_only_a_hint(self):
        self.album("Barenaked Ladies/Gordon", 12)
        found = cd.find_in_library([str(self.root)], "Barenaked Ladies", "Gordon", 15)
        self.assertEqual((found["tracks"], found["same"]), (12, False))

    def test_genre_folder_and_flat_layout(self):
        a = self.album("rock/Queen/The Miracle", 9)
        b = self.album("Peter Gabriel - So", 10)
        self.assertEqual(cd.find_in_library([str(self.root)], "Queen", "The Miracle", 9)["path"], a)
        self.assertEqual(cd.find_in_library([str(self.root)], "Peter Gabriel", "So", 10)["path"], b)

    def test_same_album_name_other_artist_is_not_a_match(self):
        self.album("Someone Else/Greatest Hits", 10)
        self.assertIsNone(cd.find_in_library([str(self.root)], "Queen", "Greatest Hits", 10))

    def test_various_artists_and_nothing_found(self):
        a = self.album("Various Artists/Hits 99", 20)
        self.assertEqual(cd.find_in_library([str(self.root)], "Various Artists", "Hits 99", 20)["path"], a)
        self.assertIsNone(cd.find_in_library([str(self.root)], "Marillion", "Brave", 10))


class Mounts(unittest.TestCase):
    def setUp(self):
        self.orig = cd.mount_fstype
        self.cfg = dict(cd.DEFAULTS, MOUNT_POINTS="/mnt/music /mnt/flac")

    def tearDown(self):
        cd.mount_fstype = self.orig

    def test_autofs_placeholder_is_not_ready(self):
        cd.mount_fstype = lambda mp: "autofs"
        self.assertFalse(cd.mounts_ready(self.cfg, [Path("/mnt/music/x")]))

    def test_real_mount_is_ready_and_unrelated_mounts_ignored(self):
        cd.mount_fstype = lambda mp: "nfs4" if mp == "/mnt/music" else None
        self.assertTrue(cd.mounts_ready(self.cfg, [Path("/mnt/music/x")]))
        self.assertFalse(cd.mounts_ready(self.cfg, [Path("/mnt/music/x"), Path("/mnt/flac/y")]))

    def test_smb_and_webdav_mounts_are_ready(self):
        for fstype in ("cifs", "smb3", "fuse", "davfs"):
            cd.mount_fstype = lambda mp, t=fstype: t
            self.assertTrue(cd.mounts_ready(self.cfg, [Path("/mnt/music/x")]), fstype)

    def test_unmounted_is_not_ready(self):
        cd.mount_fstype = lambda mp: None
        self.assertFalse(cd.mounts_ready(self.cfg, [Path("/mnt/flac/y")]))

    def test_flush_survives_oserror(self):
        d = Path(tempfile.mkdtemp())
        job = d / "jobs" / "20261108-125809-830b440a"
        job.mkdir(parents=True)
        (job / "meta.json").write_text(json.dumps({"artist": "A", "album": "B", "cls": "KIDS", "mb_id": "m",
                                                   "cddb_id": "c", "dests": {"mp3": "/mnt/x"}}), encoding="utf-8")
        orig = cd.deliver_job
        cd.deliver_job = lambda j, c: (_ for _ in ()).throw(OSError(19, "No such device"))
        try:
            rc = cd.flush(dict(cd.DEFAULTS, STATE_DIR=str(d)), cd.Status())
        finally:
            cd.deliver_job = orig
            shutil.rmtree(d)
        self.assertEqual(rc, 0)


class FuzzyMatch(unittest.TestCase):
    # Real disc: 10 tracks, TOC as read by libdiscid
    disc = cd.Disc("x", "1 10 216508 150 20320 40493 61091 78588 95312 119807 139670 161491 185629", 1, 10, "830b440a")
    lengths = [269, 269, 275, 233, 223, 327, 265, 291, 322, 412]

    def rel(self, title, artist, lengths):
        return {"id": title, "title": title, "artist-credit": [{"name": artist}],
                "media": [{"tracks": [{"length": s * 1000} for s in lengths]}]}

    def test_toc_lengths(self):
        got = cd.toc_lengths(self.disc.toc)
        self.assertEqual([round(x) for x in got], self.lengths)

    def test_picks_matching_release_not_decoy(self):
        good = self.rel("Kampf der Droiden", "Star Wars", self.lengths)
        decoy = self.rel("Burt Bacharach Live", "Trijntje", [250, 261, 270, 240, 230, 300, 270, 280, 330, 400])
        self.assertEqual(cd.best_fuzzy([decoy, good], self.disc)["title"], "Kampf der Droiden")

    def test_rejects_when_nothing_close(self):
        self.assertIsNone(cd.best_fuzzy([self.rel("A", "B", [100] * 10)], self.disc))

    def test_rejects_wrong_track_count(self):
        self.assertIsNone(cd.best_fuzzy([self.rel("A", "B", self.lengths[:9])], self.disc))

    def test_ambiguous_distinct_releases_rejected_same_release_accepted(self):
        a, b = self.rel("A", "X", self.lengths), self.rel("B", "Y", self.lengths)
        self.assertIsNone(cd.best_fuzzy([a, b], self.disc))
        self.assertIsNotNone(cd.best_fuzzy([a, dict(a, id="again")], self.disc))

    def test_attach_link(self):
        self.assertTrue(cd.attach_link(self.disc).endswith("toc=1+10+216508+150+20320+40493+61091+78588+95312+119807+139670+161491+185629"))


class GnuDB(unittest.TestCase):
    # The disc MusicBrainz did not know (2026-11-08): 10 tracks, cddb id 840d490a
    disc = cd.Disc("x", "1 10 255268 150 19139 48543 70665 94336 122413 144997 172680 199488 229236", 1, 10, "840d490a")
    offsets = [150, 19139, 48543, 70665, 94336, 122413, 144997, 172680, 199488, 229236]

    def entry(self, offsets=None, genre="Kinderh\u00f6rspiel", length=3403):
        lines = ["210 misc 840d490a CD database entry follows (until terminating `.')", "# xmcd", "#",
                 "# Track frame offsets:"] + [f"#\t{o}" for o in offsets or self.offsets]
        lines += ["#", f"# Disc length: {length} seconds", "#", "DISCID=840d490a",
                  "DTITLE=Star Wars - The Clone Wars / 07: Lange Titel werden in CDDB auf",
                  "DTITLE= mehrere Zeilen verteilt", "DYEAR=2011", f"DGENRE={genre}"]
        lines += [f"TTITLE{i}=Kapitel {i + 1}" for i in range(10)] + ["EXTD=", "PLAYORDER=", "."]
        return "\r\n".join(lines)

    def test_query_args(self):
        self.assertEqual(cd.cddb_query_args(self.disc),
                         ["840d490a", "10"] + [str(o) for o in self.offsets] + ["3403"])

    def test_parse_query_replies(self):
        self.assertEqual(cd.parse_cddb_query("200 misc 840d490a Artist / Album\r\n"), [("misc", "840d490a")])
        multi = "211 Found inexact matches, list follows (until terminating `.')\r\n" \
                "misc 840d490a A / B\r\nsoundtrack 840d490b C / D\r\n.\r\n"
        self.assertEqual(cd.parse_cddb_query(multi), [("misc", "840d490a"), ("soundtrack", "840d490b")])
        self.assertEqual(cd.parse_cddb_query("202 No match for disc ID 840d490a.\r\n"), [])
        self.assertEqual(cd.parse_cddb_query("500 Unknown application\r\n"), [])

    def test_entry_to_info(self):
        i = cd.gnudb_info(*cd.parse_xmcd(self.entry()), self.disc)
        self.assertEqual(i["artist"], "Star Wars - The Clone Wars")
        self.assertEqual(i["album"], "07: Lange Titel werden in CDDB auf mehrere Zeilen verteilt")
        self.assertEqual((i["year"], i["tracks"][9]["title"], i["match"]), ("2011", "Kapitel 10", "gnudb"))
        self.assertEqual(cd.classify(i, None, [])[0], "KIDS")

    def test_genre_decides_class(self):
        spoken = cd.gnudb_info(*cd.parse_xmcd(self.entry(genre="H\u00f6rspiel")), self.disc)
        self.assertEqual(cd.classify(spoken, None, [])[0], "UNSORTED")  # audio drama without a children's signal
        rock = cd.gnudb_info(*cd.parse_xmcd(self.entry(genre="Rock")), self.disc)
        self.assertEqual(cd.classify(rock, None, [])[0], "MUSIC")

    def test_entry_with_other_track_lengths_is_rejected(self):
        other = [150, 30000, 48543, 70665, 94336, 122413, 144997, 172680, 199488, 229236]
        self.assertIsNone(cd.gnudb_info(*cd.parse_xmcd(self.entry(offsets=other)), self.disc))
        self.assertIsNone(cd.gnudb_info(*cd.parse_xmcd(self.entry(offsets=self.offsets[:9])), self.disc))
        self.assertIsNone(cd.gnudb_info(*cd.parse_xmcd(self.entry(length=3300)), self.disc))

    def test_off_without_contact(self):
        self.assertIsNone(cd.gnudb_lookup(self.disc, {"GNUDB_CONTACT": ""}, "ua"))


class Metadata(unittest.TestCase):
    def test_parse_release_picks_matching_medium(self):
        i = cd.parse_release(MB_RELEASE, "mbdiscid", 3, media_total=2)
        self.assertEqual((i["artist"], i["year"], i["disc_no"], i["disc_total"]), ("Bibi Blocksberg", "2012", 2, 2))
        self.assertEqual([t["title"] for t in i["tracks"]], ["Intro", "Zauberei", "Ende"])
        self.assertIn("audio drama", i["types"])

    def test_track_count_mismatch_drops_titles(self):
        self.assertEqual(cd.parse_release(MB_RELEASE, "mbdiscid", 5)["tracks"], [])

    def test_xmcd_known(self):
        x = cd.build_xmcd(DISC, info())
        self.assertIn("DISCID=a10b0c03", x)
        self.assertIn("DTITLE=Bibi Blocksberg / Folge 1: Hexenzauber (Disc 2)", x)
        self.assertIn("TTITLE2=Ende", x)
        self.assertIn("DGENRE=Speech", x)

    def test_xmcd_various_artists(self):
        i = info(artist="Various Artists", types=["album"])
        i["tracks"][0]["artist"] = "Band A"
        x = cd.build_xmcd(DISC, i)
        self.assertIn("DTITLE=Various Artists / ", x)
        self.assertIn("TTITLE0=Band A / Intro", x)

    def test_xmcd_unknown(self):
        x = cd.build_xmcd(DISC, None, now="2026-01-01 10:00")
        self.assertIn("DTITLE=Unbekannt / Disc 2026-01-01 10:00", x)
        self.assertIn("TTITLE2=Track 03", x)

    def test_config_and_formats(self):
        d = tempfile.mkdtemp()
        p = Path(d) / "c.conf"
        p.write_text('# x\nDEST_KIDS_FLAC="/mnt/x"\nMB_CONTACT=a@b.c\n', encoding="utf-8")
        cfg = cd.load_config(p)
        self.assertEqual(cfg["MB_CONTACT"], "a@b.c")
        self.assertEqual(cd.formats_for(cfg, "KIDS"), ["mp3", "flac"])
        self.assertEqual(cd.formats_for(cfg, "MUSIC"), ["mp3"])  # MP3 only unless a FLAC line is set
        self.assertEqual(cd.formats_for(cfg, "SPOKEN"), ["mp3"])
        shutil.rmtree(d)



class DeliveryAndTags(unittest.TestCase):
    """Needs lame, flac and python3-mutagen (present on the Pi)."""

    def setUp(self):
        try:
            import mutagen  # noqa: F401
        except ImportError:
            self.skipTest("mutagen missing")
        if not (shutil.which("lame") and shutil.which("flac")):
            self.skipTest("lame/flac missing")
        self.tmp = Path(tempfile.mkdtemp())

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def make_wav(self, p):
        import wave
        with wave.open(str(p), "wb") as w:
            w.setnchannels(2); w.setsampwidth(2); w.setframerate(44100)
            w.writeframes(b"\x00\x00" * 2 * 4410)

    def test_retag_deliver_roundtrip(self):
        import mutagen
        job = self.tmp / "job"
        wav = self.tmp / "t.wav"
        self.make_wav(wav)
        for fmt in ("mp3", "flac"):
            d = job / "out" / fmt / "Bibi Blocksberg" / "Folge 1 Hexenzauber (Disc 2)"
            d.mkdir(parents=True)
            for n in (1, 2, 3):
                f = d / f"{n:02d} - x.{fmt}"
                if fmt == "mp3":
                    subprocess.run(["lame", "--quiet", str(wav), str(f)], check=True)
                else:
                    subprocess.run(["flac", "-s", str(wav), "-o", str(f)], check=True)
            cover = self.tmp / "cover.jpg"
            cover.write_bytes(b"\xff\xd8\xff\xe0" + b"0" * 100)
            cd.retag(d, fmt, info(), DISC, cover)
            first = sorted(d.glob(f"*.{fmt}"))[1]
            tags = mutagen.File(first, easy=(fmt == "mp3"))
            self.assertEqual(tags["title"][0], "Zauberei")
            self.assertEqual(tags["album"][0], "Folge 1: Hexenzauber (Disc 2)")
        dest = self.tmp / "nas"
        (job / "meta.json").write_text(json.dumps({
            "mb_id": "m", "cddb_id": "c", "cls": "KIDS", "artist": "Bibi Blocksberg", "album": "x",
            "dests": {"mp3": str(dest / "kids"), "flac": str(dest / "flac")}}), encoding="utf-8")
        state = self.tmp / "state"; state.mkdir()
        cfg = dict(cd.DEFAULTS, STATE_DIR=str(state), MOUNT_POINTS="")
        final = cd.deliver_job(job, cfg)
        self.assertEqual(len(final), 2)
        self.assertTrue((dest / "kids" / "Bibi Blocksberg" / "Folge 1 Hexenzauber (Disc 2)" / "01 - x.mp3").exists())
        self.assertFalse(job.exists())
        self.assertIn("Bibi Blocksberg", (state / "done.tsv").read_text(encoding="utf-8"))

    def test_relabel_job_renames_retags_reclasses(self):
        import mutagen
        job = self.tmp / "job"
        wav = self.tmp / "t.wav"
        self.make_wav(wav)
        for fmt in ("mp3", "flac"):
            d = job / "out" / fmt / "Unbekannt" / "Disc 2026"
            d.mkdir(parents=True)
            for n in (1, 2, 3):
                f = d / f"{n:02d} - Track {n:02d}.{fmt}"
                subprocess.run(["lame", "--quiet", str(wav), str(f)] if fmt == "mp3"
                               else ["flac", "-s", str(wav), "-o", str(f)], check=True)
        (job / "meta.json").write_text(json.dumps({
            "mb_id": "m", "cddb_id": "c", "cls": "UNSORTED", "artist": "Unbekannt", "album": "x",
            "dests": {"mp3": "/a", "flac": "/b"}}), encoding="utf-8")
        cfg = dict(cd.DEFAULTS, DEST_KIDS_MP3=str(self.tmp / "kids"))
        cfg.pop("DEST_KIDS_FLAC", None)
        i = info(has_cover=False, album="Kampf: der / Droiden?")
        cd.relabel_job(job, i, "KIDS", cfg, "test")
        self.assertFalse((job / "out" / "flac").exists())          # KIDS is MP3 only
        album = job / "out" / "mp3" / "Bibi Blocksberg" / "Kampf der - Droiden (Disc 2)"
        self.assertEqual(sorted(p.name for p in album.glob("*.mp3")),
                         ["01 - Intro.mp3", "02 - Zauberei.mp3", "03 - Ende.mp3"])
        self.assertEqual(mutagen.File(album / "02 - Zauberei.mp3", easy=True)["title"][0], "Zauberei")
        meta = json.loads((job / "meta.json").read_text(encoding="utf-8"))
        self.assertEqual((meta["cls"], list(meta["dests"])), ("KIDS", ["mp3"]))

    def test_medium_fallback_by_track_count(self):
        i = cd.parse_release(MB_RELEASE, "other-disc", 3)
        self.assertEqual(len(i["tracks"]), 3)

    def test_unique_target(self):
        p = self.tmp / "a"; p.mkdir()
        self.assertEqual(cd.unique_target(p).name, "a (2)")


class Cancel(unittest.TestCase):
    """SIGTERM during the rip stops abcde and everything it started, and comes out as Cancelled."""

    def test_sigterm_stops_the_whole_rip(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            (tmp / "bin").mkdir()
            fake = tmp / "bin" / "abcde"  # like abcde: a shell that runs cdparanoia (here: sleep)
            fake.write_text(f"#!/bin/sh\nsleep 60 &\necho $! > {tmp}/child\nwait\n")
            fake.chmod(0o755)
            (tmp / "job").mkdir()
            script = f"""
import importlib.machinery, importlib.util, signal, sys
loader = importlib.machinery.SourceFileLoader("cd_rip", {str(ROOT / "bin" / "cd-rip")!r})
spec = importlib.util.spec_from_loader("cd_rip", loader); cd = importlib.util.module_from_spec(spec); loader.exec_module(cd)
signal.signal(signal.SIGTERM, cd._on_term)
disc = cd.Disc("x", "1 3 1000 150 400 700", 1, 3, "a10b0c03")
try:
    cd.rip("/dev/null", __import__("pathlib").Path({str(tmp / "job")!r}), ["mp3"], disc, cd.Status(), dict(ABCDE_CONF="x", STATE_DIR={str(tmp)!r}), {{}})
except cd.Cancelled:
    sys.exit(42)
"""
            env = dict(os.environ, PATH=f"{tmp}/bin:" + os.environ["PATH"], CDRIP_RUN_DIR=str(tmp / "run"))
            p = subprocess.Popen([sys.executable, "-I", "-c", script], env=env)
            for _ in range(100):
                if (tmp / "child").exists() and (tmp / "child").read_text().strip():
                    break
                time.sleep(0.05)
            child = int((tmp / "child").read_text())
            p.terminate()
            self.assertEqual(p.wait(15), 42)
            self.assertTrue(gone(child), "cdparanoia (here: sleep) survived the cancel")


def gone(pid, wait=5.0):
    """True once pid has ended. A zombie counts: in a container, PID 1 may never collect it."""
    end = time.time() + wait
    while time.time() < end:
        try:
            state = Path(f"/proc/{pid}/stat").read_text().rsplit(")", 1)[1].split()[0]
        except (OSError, IndexError):
            return True
        if state in ("Z", "X"):
            return True
        time.sleep(0.05)
    return False


if __name__ == "__main__":
    unittest.main()
