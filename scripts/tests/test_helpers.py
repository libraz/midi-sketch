"""Tests for helper functions (note_name, tick_to_bar)."""

import unittest

from conftest import note_name, tick_to_bar


class TestNoteHelpers(unittest.TestCase):
    """Test helper functions."""

    def test_note_name(self):
        self.assertEqual(note_name(60), "C4")
        self.assertEqual(note_name(69), "A4")
        self.assertEqual(note_name(36), "C2")
        self.assertEqual(note_name(84), "C6")

    def test_tick_to_bar(self):
        self.assertEqual(tick_to_bar(0), 1)
        self.assertEqual(tick_to_bar(1919), 1)
        self.assertEqual(tick_to_bar(1920), 2)
        self.assertEqual(tick_to_bar(3840), 3)


if __name__ == "__main__":
    unittest.main()
