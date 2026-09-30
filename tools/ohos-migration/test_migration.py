import hashlib
import json
from pathlib import Path
import shutil
import sqlite3
import tempfile
import threading
import unittest
import zipfile

from migration_core import Cancelled, Source, parse_config, safe_name, verify_package


def make_fixture(directory):
    root = Path(directory) / "配置"
    root.mkdir()
    (root / "mixxx.cfg").write_text("[Config]\nVersion 2.5.6\nSkin Deere\nPassword secret\n[Controls]\nCueMode 2\nRecordingDirectory D:/private\n", encoding="utf-8")
    con = sqlite3.connect(root / "mixxxdb.sqlite")
    con.executescript("CREATE TABLE settings(name TEXT,value TEXT); INSERT INTO settings VALUES('mixxx.schema.version','39'); CREATE TABLE directories(directory TEXT); INSERT INTO directories VALUES('D:/Music'); INSERT INTO directories VALUES('D:/Music/FAST'); CREATE TABLE track_locations(id INTEGER PRIMARY KEY,location TEXT,filename TEXT,filesize INTEGER); CREATE TABLE library(id INTEGER PRIMARY KEY,location INTEGER,coverart_location TEXT); CREATE TABLE cues(id INTEGER,track_id INTEGER,position INTEGER); INSERT INTO cues VALUES(7,10,123456); CREATE TABLE Playlists(id INTEGER,name TEXT); INSERT INTO Playlists VALUES(3,'歌单'); CREATE TABLE PlaylistTracks(playlist_id INTEGER,track_id INTEGER,position INTEGER); INSERT INTO PlaylistTracks VALUES(3,10,1); CREATE TABLE track_analysis(id INTEGER); INSERT INTO track_analysis VALUES(12);")
    rows = [(10, 1, "D:/Music/同名.wav", 32), (11, 2, "D:/Music/FAST/同名.wav", 48), (12, 3, "Z:/Other/missing.wav", 12)]
    for tid, lid, path, size in rows:
        con.execute("INSERT INTO track_locations VALUES(?,?,?,?)", (lid, path, path.rsplit("/", 1)[1], size))
        con.execute("INSERT INTO library VALUES(?,?,?)", (tid, lid, ""))
    con.commit()
    con.close()
    music = Path(directory) / "音乐"
    fast = Path(directory) / "快歌"
    music.mkdir()
    fast.mkdir()
    (music / "同名.wav").write_bytes(b"a" * 32)
    (fast / "同名.wav").write_bytes(b"b" * 48)
    (root / "samplers.xml").write_text('<samplerbank><sampler group="[Sampler1]" location="D:/Music/FAST/同名.wav"/></samplerbank>', encoding="utf-8")
    return root, music, fast


class MigrationTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root, self.music, self.fast = make_fixture(self.temp.name)
        self.source = Source(self.root)
        self.source.match({"D:/Music": str(self.music), "D:/Music/FAST": str(self.fast)})

    def tearDown(self):
        self.source.close()
        self.temp.cleanup()

    def test_longest_root_duplicate_filename_missing_and_ids(self):
        self.assertEqual([t["status"] for t in self.source.tracks], ["matched", "matched", "missing"])
        output = Path(self.temp.name) / "迁移包.zip"
        manifest = self.source.create_package(output)
        self.assertEqual((manifest["includedTracks"], manifest["missingTracks"]), (2, 1))
        self.assertNotEqual(manifest["tracks"][0]["audioPath"], manifest["tracks"][1]["audioPath"])
        with zipfile.ZipFile(output) as z:
            db = Path(self.temp.name) / "copy.sqlite"
            db.write_bytes(z.read("config/Mixxx/mixxxdb.sqlite"))
            con = sqlite3.connect(db)
            self.assertEqual(con.execute("SELECT * FROM cues").fetchall(), [(7, 10, 123456)])
            self.assertEqual(con.execute("SELECT * FROM PlaylistTracks").fetchall(), [(3, 10, 1)])
            self.assertEqual(con.execute("SELECT count(*) FROM track_analysis").fetchone()[0], 0)
            con.close()
            self.assertNotIn(b"secret", z.read("config/Mixxx/mixxx.cfg"))
            self.assertIn(b"@transfer/audio/location-2", z.read("config/Mixxx/samplers.xml"))
            self.assertEqual(z.read(manifest["tracks"][0]["audioPath"]), b"a" * 32)
            self.assertEqual(z.read(manifest["tracks"][1]["audioPath"]), b"b" * 48)
            local = output.read_bytes()
            offset = z.getinfo(manifest["tracks"][0]["audioPath"]).header_offset
            self.assertEqual(local[offset + 18:offset + 26], b"\xff" * 8)

    def test_changed_requires_confirmation_and_mapping_keeps_it(self):
        (self.music / "同名.wav").write_bytes(b"different")
        self.source.match(self.source.mapping)
        self.assertEqual(self.source.tracks[0]["status"], "changed")
        self.source.choose_file(10, self.music / "同名.wav")
        self.source.match(self.source.mapping)
        self.assertEqual(self.source.tracks[0]["status"], "confirmed")

    def test_cancel_keeps_existing_output(self):
        output = Path(self.temp.name) / "output.zip"
        output.write_bytes(b"original")
        cancel = threading.Event()
        cancel.set()
        with self.assertRaises(Cancelled):
            self.source.create_package(output, cancel=cancel)
        self.assertEqual(output.read_bytes(), b"original")
        self.assertFalse(list(output.parent.glob("*.partial-*")))

    def test_verify_cancel_and_tampering(self):
        output = Path(self.temp.name) / "output.zip"
        self.source.create_package(output)
        cancel = threading.Event()
        cancel.set()
        with self.assertRaises(Cancelled):
            verify_package(output, cancel)
        with zipfile.ZipFile(output) as z:
            items = {name: z.read(name) for name in z.namelist()}
        items["audio/location-1/同名.wav"] = b"z" * 32
        with zipfile.ZipFile(output, "w") as z:
            for name, data in items.items():
                z.writestr(name, data)
        with self.assertRaises(ValueError):
            verify_package(output)

    def test_unsafe_source_zip(self):
        output = Path(self.temp.name) / "unsafe.zip"
        with zipfile.ZipFile(output, "w") as z:
            z.writestr("../escape", "bad")
        with self.assertRaises(ValueError):
            Source(output)
        for name in ("/absolute", "../a", "a/../b", "C:/a", "a//b"):
            with self.assertRaises(ValueError):
                safe_name(name)

    def test_source_not_modified(self):
        before = hashlib.sha256((self.root / "mixxxdb.sqlite").read_bytes()).hexdigest()
        self.source.create_package(Path(self.temp.name) / "output.zip", include_music=False)
        self.assertEqual(before, hashlib.sha256((self.root / "mixxxdb.sqlite").read_bytes()).hexdigest())


if __name__ == "__main__":
    unittest.main()
