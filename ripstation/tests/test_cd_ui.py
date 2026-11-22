import importlib.machinery
import importlib.util
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
loader = importlib.machinery.SourceFileLoader("cd_ui", str(ROOT / "bin" / "cd-ui"))
spec = importlib.util.spec_from_loader("cd_ui", loader)
ui = importlib.util.module_from_spec(spec)
loader.exec_module(ui)


class FakeBus:
    """Records what an HD44780 would latch: (rs, nibble) on every falling edge of E."""
    def __init__(self):
        self.pins, self.latched = {}, []

    def factory(self, name):
        bus = self

        class Pin:
            def __init__(self, num, initial_value=False):
                self.num, self._v = num, initial_value
                bus.pins[num] = initial_value

            @property
            def value(self):
                return self._v

            @value.setter
            def value(self, v):
                self._v = bool(v)
                bus.pins[self.num] = bool(v)

            def on(self):
                self.value = True

            def off(self):
                self.value = False
                if self.num == name["e"]:
                    nib = sum(int(bus.pins[p]) << i for i, p in enumerate(name["d"]))
                    bus.latched.append((bus.pins[name["rs"]], nib))
        return Pin


class Lcd(unittest.TestCase):
    def make(self):
        pins = dict(rs=22, e=23, d=[24, 25, 5, 6])
        bus = FakeBus()
        lcd = ui.Lcd1602([22, 23, 24, 25, 5, 6], factory=bus.factory(pins))
        return lcd, bus

    @staticmethod
    def bytes_(latched):
        out, it = [], iter(latched)
        for (rs, hi), (_, lo) in zip(it, it):
            out.append((rs, hi << 4 | lo))
        return out

    def test_init_sequence(self):
        lcd, bus = self.make()
        self.assertEqual(bus.latched[:4], [(False, 3), (False, 3), (False, 3), (False, 2)])
        self.assertEqual(self.bytes_(bus.latched[4:]), [(False, 0x28), (False, 0x0C), (False, 0x06), (False, 0x01)])

    def test_show_writes_both_lines_and_skips_repeats(self):
        lcd, bus = self.make()
        bus.latched.clear()
        lcd.show("Hallo".ljust(16), "Welt".ljust(16))
        b = self.bytes_(bus.latched)
        self.assertEqual(b[0], (False, 0x80))
        self.assertEqual(b[1:6], [(True, ord(c)) for c in "Hallo"])
        self.assertEqual(b[17], (False, 0xC0))
        self.assertEqual(len(b), 2 + 32)
        bus.latched.clear()
        lcd.show("Hallo".ljust(16), "Welt".ljust(16))
        self.assertEqual(bus.latched, [])


class Text(unittest.TestCase):
    def test_encode_umlauts_a00(self):
        self.assertEqual(ui.lcd_encode("äöüß"), bytes([0xE1, 0xEF, 0xF5, 0xE2]))
        self.assertEqual(ui.lcd_encode("Ä€"), b"A?")

    def test_lines_are_16_wide(self):
        for st in ({"state": "idle"}, {"state": "reading", "message": "x" * 40},
                   {"state": "ripping", "progress": 0.456, "artist": "A" * 30, "album": "B"},
                   {"state": "done", "cls": "Kinderhörspiel", "album": "Kampf der Droiden"},
                   {"state": "error", "message": "NAS nicht erreichbar - wird später kopiert"}):
            for page in (0, 1):
                l1, l2 = ui.lcd_lines(st, "KIDS", "192.168.1.20", page)
                self.assertEqual((len(l1), len(l2)), (16, 16), st)

    def test_progress_bar(self):
        _, l2 = ui.lcd_lines({"state": "ripping", "progress": 0.456}, None, "ip")
        self.assertEqual(l2, "[#####     ] 45%")

    def test_idle_shows_choice(self):
        _, l2 = ui.lcd_lines({"state": "idle"}, "KIDS", "ip")
        self.assertTrue(l2.startswith("Naechste: Kinder"))


class Beeps(unittest.TestCase):
    def test_patterns(self):
        self.assertEqual(ui.beep_pattern("idle", "ripping"), (0.08, 0.1, 1))
        self.assertEqual(ui.beep_pattern("delivering", "done")[2], 2)
        self.assertEqual(ui.beep_pattern("ripping", "error")[2], 3)

    def test_no_beep_without_change_or_for_quiet_states(self):
        self.assertIsNone(ui.beep_pattern("done", "done"))
        self.assertIsNone(ui.beep_pattern("done", "idle"))
        self.assertIsNone(ui.beep_pattern("ripping", "delivering"))
        self.assertIsNone(ui.beep_pattern("idle", "reading"))
        self.assertIsNone(ui.beep_pattern("done", "done", 5.0, 5.0))

    def test_already_in_library_beeps_even_if_the_poll_missed_the_reading_state(self):
        self.assertEqual(ui.beep_pattern("done", "done", 5.0, 9.0, "Schon vorhanden"), ui.ALREADY_THERE)
        self.assertEqual(ui.beep_pattern("reading", "done", 5.0, 9.0, "Schon in der Bibliothek"), ui.ALREADY_THERE)
        self.assertEqual(ui.beep_pattern("error", "error", 5.0, 9.0)[2], 3)
        self.assertEqual(ui.beep_pattern("done", "done", 5.0, 9.0, "Fertig")[2], 2)


if __name__ == "__main__":
    unittest.main()
