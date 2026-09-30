import hashlib
import json
import os
import re
from pathlib import Path, PurePosixPath, PureWindowsPath
import shutil
import sqlite3
import subprocess
import sys
import tempfile
import threading
import uuid
import zipfile
import xml.etree.ElementTree as ET

FORMAT = "pomelo-mixxx-transfer"
VERSION = 1
BLOCK = 1024 * 1024


class Cancelled(Exception):
    pass


def safe_name(name):
    name = name.replace("\\", "/")
    p = PurePosixPath(name)
    if not name or name.startswith("/") or ":" in name or "\x00" in name or any(x in ("", ".", "..") for x in name.rstrip("/").split("/")):
        raise ValueError("压缩包包含不安全的路径")
    return p.as_posix()


def parse_config(path):
    groups = {}
    section = None
    for line in Path(path).read_text(encoding="utf-8-sig").splitlines():
        if line.startswith("[") and line.endswith("]"):
            section = line
            groups.setdefault(section, {})
        elif section and line.strip() and not line.startswith(("#", ";")):
            pair = line.split(None, 1)
            groups[section][pair[0]] = pair[1] if len(pair) == 2 else ""
    return groups


def portable_config(groups):
    result = {}
    for group, values in groups.items():
        if any(x in group.lower() for x in ("broadcast", "soundcard", "controller", "sandbox", "ohos")):
            continue
        kept = {}
        for key, value in values.items():
            low = key.lower()
            if any(x in low for x in ("password", "token", "secret", "credential", "directory", "path", "location", "filename", "geometry")):
                continue
            if re.search(r"(^|\s)[A-Za-z]:[/\\]|^\\\\", value):
                continue
            if group == "[Config]" and key in ("ScaleFactor", "StartInFullscreen", "hide_menubar"):
                continue
            kept[key] = value
        if kept:
            result[group] = kept
    result.setdefault("[Config]", {}).update(ScaleFactor="0.75", hide_menubar="0", show_menubar_hint="0")
    return result


def config_text(groups):
    return "\n\n".join(group + "\n" + "\n".join(k + " " + v for k, v in values.items()) for group, values in groups.items()) + "\n"


class Source:
    def __init__(self, path):
        self.temp = tempfile.TemporaryDirectory(prefix="mixxx-transfer-")
        self.work = Path(self.temp.name)
        try:
            path = Path(path)
            if path.is_dir():
                root = path
            elif path.suffix.lower() == ".cfg":
                root = path.parent
            else:
                root = self.work / "source"
                root.mkdir()
                if zipfile.is_zipfile(path):
                    with zipfile.ZipFile(path) as z:
                        if len(z.infolist()) > 50000 or sum(i.file_size for i in z.infolist()) > 8 * 1024 ** 3:
                            raise ValueError("配置备份过大，请选择原配置目录")
                        for i in z.infolist():
                            name = safe_name(i.filename)
                            if (i.external_attr >> 16) & 0o170000 == 0o120000:
                                raise ValueError("配置备份包含符号链接")
                            dest = root / name
                            if i.is_dir():
                                dest.mkdir(parents=True, exist_ok=True)
                            else:
                                dest.parent.mkdir(parents=True, exist_ok=True)
                                with z.open(i) as inp, dest.open("wb") as out:
                                    shutil.copyfileobj(inp, out, BLOCK)
                else:
                    exe_root = Path(sys.executable).parent
                    bundled = Path(getattr(sys, "_MEIPASS", Path(__file__).parent)) / "7zip" / "7z.exe"
                    candidates = [bundled, exe_root / "7zip" / "7z.exe", Path("C:/Program Files/7-Zip/7z.exe")]
                    seven = next((x for x in candidates if x.is_file()), None)
                    if not seven:
                        raise ValueError("RAR 读取组件缺失，请使用完整便携工具或先解压备份")
                    flags = {"creationflags": subprocess.CREATE_NO_WINDOW} if os.name == "nt" else {}
                    listing = subprocess.run([str(seven), "l", "-slt", "-sccUTF-8", str(path)], capture_output=True, timeout=60, **flags)
                    if listing.returncode:
                        raise ValueError("无法读取该备份，请检查格式或密码")
                    body = listing.stdout.decode("utf-8", errors="strict").split("----------", 1)[-1]
                    total = 0
                    count = 0
                    for line in body.splitlines():
                        if line.startswith("Path = "):
                            safe_name(line[7:])
                            count += 1
                        elif line.startswith("Size = "):
                            total += int(line[7:])
                        elif line.startswith(("Symbolic Link = ", "Hard Link = ")) and line.split(" = ", 1)[1].strip():
                            raise ValueError("配置备份包含链接")
                    if total > 8 * 1024 ** 3 or count > 50000:
                        raise ValueError("配置备份过大，请选择原配置目录")
                    extraction = subprocess.run([str(seven), "x", "-y", "-o" + str(root), str(path)], capture_output=True, timeout=300, **flags)
                    if extraction.returncode:
                        raise ValueError("备份解压失败")
            cfgs = [root / "mixxx.cfg"] if (root / "mixxx.cfg").is_file() else list(root.rglob("mixxx.cfg"))
            if len(cfgs) != 1:
                raise ValueError("请选择一份包含 mixxx.cfg 的配置备份")
            self.root = cfgs[0].parent
            self.groups = parse_config(cfgs[0])
            self.settings = portable_config(self.groups)
            db = self.root / "mixxxdb.sqlite"
            self.tracks = []
            self.roots = []
            self.schema = None
            self.database = None
            if db.is_file():
                self.database = self.work / "mixxxdb.sqlite"
                source = sqlite3.connect(db.resolve().as_uri() + "?mode=ro", uri=True)
                try:
                    if source.execute("PRAGMA quick_check").fetchone()[0] != "ok":
                        raise ValueError("曲库数据库校验失败")
                    dest = sqlite3.connect(self.database)
                    try:
                        source.backup(dest)
                    finally:
                        dest.close()
                    self.schema = source.execute("SELECT value FROM settings WHERE name='mixxx.schema.version'").fetchone()[0]
                    self.roots = [r[0].replace("\\", "/").rstrip("/") for r in source.execute("SELECT directory FROM directories")]
                    for row in source.execute("SELECT l.id,t.id,t.location,t.filename,t.filesize FROM library l JOIN track_locations t ON t.id=l.location ORDER BY l.id"):
                        tid, lid, old, name, size = row
                        self.tracks.append({"trackId": tid, "locationId": lid, "original": old.replace("\\", "/"), "filename": name, "recordedSize": size, "resolved": None, "status": "missing"})
                    self.cue_count = source.execute("SELECT count(*) FROM cues").fetchone()[0]
                    self.playlist_count = source.execute("SELECT count(*) FROM Playlists").fetchone()[0]
                finally:
                    source.close()
                for track in self.tracks:
                    if not any(track["original"].casefold().startswith(r.casefold() + "/") for r in self.roots):
                        parent = str(PureWindowsPath(track["original"]).parent).replace("\\", "/")
                        if parent not in self.roots:
                            self.roots.append(parent)
            self.mapping = {r: r for r in self.roots if Path(r).is_dir()}
        except Exception:
            self.temp.cleanup()
            raise

    def close(self):
        self.temp.cleanup()

    def match(self, mapping):
        self.mapping = dict(mapping)
        for track in self.tracks:
            if track["status"] == "confirmed" and Path(track["resolved"]).is_file():
                continue
            track["resolved"] = None
            track["status"] = "missing"
            old = track["original"]
            for prefix in sorted(mapping, key=len, reverse=True):
                if old.casefold().startswith(prefix.casefold().rstrip("/") + "/"):
                    relative = old[len(prefix.rstrip("/")) + 1:]
                    safe_name(relative)
                    target = Path(mapping[prefix]) / relative
                    if target.is_file() and not target.is_symlink():
                        track["resolved"] = str(target.resolve())
                        track["status"] = "matched" if target.stat().st_size == track["recordedSize"] else "changed"
                    break
            if not track["resolved"] and Path(old).is_file():
                track["resolved"] = str(Path(old).resolve())
                track["status"] = "matched" if Path(old).stat().st_size == track["recordedSize"] else "changed"
        return self.tracks

    def choose_file(self, track_id, path):
        path = Path(path).resolve()
        if not path.is_file():
            raise ValueError("请选择可读取的音乐文件")
        next(t for t in self.tracks if t["trackId"] == track_id).update(resolved=str(path), status="confirmed")

    def resolve_resource(self, original):
        normalized = original.replace("\\", "/")
        for root in sorted(self.mapping, key=len, reverse=True):
            if normalized.casefold().startswith(root.casefold().rstrip("/") + "/"):
                relative = normalized[len(root.rstrip("/")) + 1:]
                safe_name(relative)
                candidate = Path(self.mapping[root]) / relative
                return candidate if candidate.is_file() else None
        candidate = Path(original)
        return candidate if candidate.is_file() else None

    def create_package(self, output, include_music=True, include_cache=False, progress=None, cancel=None, allow_changed=False):
        output = Path(output).resolve()
        output.parent.mkdir(parents=True, exist_ok=True)
        cancelled = cancel or threading.Event()
        package_id = uuid.uuid4().hex
        staging = output.with_name(output.name + ".partial-" + package_id)
        entries = []
        tracks = []
        resources = []
        generated = self.work / "generated"
        generated.mkdir(exist_ok=True)
        (generated / "mixxx.cfg").write_text(config_text(self.settings), encoding="utf-8")
        (generated / "portable-settings.json").write_text(json.dumps(self.settings, ensure_ascii=False), encoding="utf-8")
        files = [(generated / "mixxx.cfg", "config/Mixxx/mixxx.cfg", "config"), (generated / "portable-settings.json", "portable-settings.json", "settings")]
        if self.database:
            dbcopy = generated / "mixxxdb.sqlite"
            shutil.copyfile(self.database, dbcopy)
            if not include_cache:
                con = sqlite3.connect(dbcopy)
                try:
                    con.execute("DELETE FROM track_analysis")
                    con.commit()
                finally:
                    con.close()
            files.append((dbcopy, "config/Mixxx/mixxxdb.sqlite", "database"))
        used = {}
        for track in self.tracks:
            audio = None
            eligible = track["status"] in ("matched", "confirmed") or (allow_changed and track["status"] == "changed")
            if include_music and eligible:
                lid = track["locationId"]
                audio = used.get(lid)
                if not audio:
                    audio = "audio/location-" + str(lid) + "/" + safe_name(PureWindowsPath(track["original"]).name)
                    files.append((Path(track["resolved"]), audio, "audio"))
                    used[lid] = audio
            tracks.append({"trackId": track["trackId"], "locationId": track["locationId"], "original": track["original"], "audioPath": audio, "status": "included" if audio else "not_selected" if not include_music else "changed" if track["status"] == "changed" else "missing"})
        for path in sorted(self.root.rglob("*")):
            if not path.is_file() or path.is_symlink():
                continue
            rel = path.relative_to(self.root).as_posix()
            if rel == "effects.xml" or rel.startswith(("effects/", "controllers/")) or (include_cache and rel.startswith("analysis/")) or path.name.endswith(".kbd.cfg"):
                if path.stat().st_size > 256 * 1024 ** 2:
                    raise ValueError("配置资源文件过大")
                files.append((path, "config/Mixxx/" + safe_name(rel), "resource"))
        if (self.root / "samplers.xml").is_file():
            bank = ET.parse(self.root / "samplers.xml")
            for number, slot in enumerate(bank.iter("sampler")):
                original = slot.get("location", "")
                audio = next((t["audioPath"] for t in tracks if t["original"].casefold() == original.replace("\\", "/").casefold()), None)
                resource = self.resolve_resource(original) if original else None
                if not audio and include_music and resource:
                    audio = "audio/sampler-" + str(number) + "/" + safe_name(resource.name)
                    files.append((resource, audio, "audio"))
                slot.set("location", "@transfer/" + audio if audio else "")
            bank.write(generated / "samplers.xml", encoding="utf-8", xml_declaration=True)
            files.append((generated / "samplers.xml", "config/Mixxx/samplers.xml", "resource"))
        if self.database:
            con = sqlite3.connect(self.database.as_uri() + "?mode=ro", uri=True)
            try:
                for tid, cover in con.execute("SELECT id,coverart_location FROM library WHERE coverart_location IS NOT NULL AND coverart_location!=''"):
                    track = next((t for t in self.tracks if t["trackId"] == tid), None)
                    if track and track["resolved"]:
                        original_cover = Path(cover)
                        source_cover = self.resolve_resource(cover) if PureWindowsPath(cover).is_absolute() else Path(track["resolved"]).parent / cover
                        if source_cover and source_cover.is_file():
                            target_cover = "artwork/track-" + str(tid) + "/" + safe_name(source_cover.name)
                            files.append((source_cover, target_cover, "artwork"))
                            resources.append({"trackId": tid, "coverPath": target_cover})
            finally:
                con.close()
        total = sum(p.stat().st_size for p, _, _ in files)
        if shutil.disk_usage(output.parent).free < total + 64 * 1024 ** 2:
            raise ValueError("输出目录空间不足")
        done = 0
        try:
            with zipfile.ZipFile(staging, "w", compression=zipfile.ZIP_STORED, allowZip64=True) as z:
                for path, name, role in files:
                    if cancelled.is_set():
                        raise Cancelled("已取消")
                    stat = path.stat()
                    digest = hashlib.sha256()
                    info = zipfile.ZipInfo(safe_name(name))
                    info.file_size = stat.st_size
                    info.external_attr = 0o100600 << 16
                    with path.open("rb") as src, z.open(info, "w", force_zip64=True) as dest:
                        count = 0
                        while block := src.read(BLOCK):
                            if cancelled.is_set():
                                raise Cancelled("已取消")
                            dest.write(block)
                            digest.update(block)
                            count += len(block)
                            done += len(block)
                            if progress:
                                progress(done, total, path.name)
                    after = path.stat()
                    if count != stat.st_size or after.st_size != stat.st_size or after.st_mtime_ns != stat.st_mtime_ns:
                        raise ValueError("打包时文件发生变化，请关闭相关程序后重试")
                    entries.append({"path": name, "size": count, "sha256": digest.hexdigest(), "role": role})
                manifest = {"format": FORMAT, "version": VERSION, "packageId": package_id, "sourceVersion": self.groups.get("[Config]", {}).get("Version", ""), "sourceSchema": self.schema, "files": entries, "tracks": tracks, "covers": resources, "includedTracks": sum(bool(t["audioPath"]) for t in tracks), "missingTracks": sum(not t["audioPath"] for t in tracks), "roots": self.roots}
                z.writestr("manifest.json", json.dumps(manifest, ensure_ascii=False).encode("utf-8"))
            verify_package(staging, cancelled, progress)
            os.replace(staging, output)
            return manifest
        finally:
            staging.unlink(missing_ok=True)


def verify_package(path, cancel=None, progress=None):
    with zipfile.ZipFile(path) as z:
        names = z.namelist()
        if len(names) != len(set(names)):
            raise ValueError("迁移包有重复条目")
        for name in names:
            safe_name(name)
        if z.getinfo("manifest.json").file_size > 16 * 1024 ** 2:
            raise ValueError("迁移清单过大")
        manifest = json.loads(z.read("manifest.json"))
        if manifest.get("format") != FORMAT or manifest.get("version") != VERSION:
            raise ValueError("不支持的迁移包格式")
        if len(names) > 50001 or len(manifest["files"]) > 50000:
            raise ValueError("迁移包文件数量过多")
        package_id = manifest.get("packageId", "")
        if len(package_id) != 32 or any(c not in "0123456789abcdef" for c in package_id):
            raise ValueError("无效的迁移包标识")
        declared = {f["path"] for f in manifest["files"]}
        if set(names) != declared | {"manifest.json"} or len(declared) != len(manifest["files"]):
            raise ValueError("迁移清单与文件不一致")
        total = sum(f["size"] for f in manifest["files"])
        done = 0
        roles = {}
        for f in manifest["files"]:
            name = safe_name(f["path"])
            role = f["role"]
            permitted = (role == "settings" and name == "portable-settings.json") or (role == "config" and name == "config/Mixxx/mixxx.cfg") or (role == "database" and name == "config/Mixxx/mixxxdb.sqlite") or (role == "resource" and name.startswith("config/Mixxx/")) or (role == "audio" and name.startswith("audio/")) or (role == "artwork" and name.startswith("artwork/"))
            if not permitted or z.getinfo(name).compress_type != zipfile.ZIP_STORED or z.getinfo(name).file_size != f["size"]:
                raise ValueError("迁移文件类型或大小不正确")
            roles[name] = role
            digest = hashlib.sha256()
            count = 0
            with z.open(f["path"]) as src:
                while block := src.read(BLOCK):
                    if cancel and cancel.is_set():
                        raise Cancelled("已取消")
                    digest.update(block)
                    count += len(block)
                    done += len(block)
                    if progress:
                        progress(done, total, "校验 " + PurePosixPath(name).name)
            if count != f["size"] or digest.hexdigest() != f["sha256"]:
                raise ValueError("迁移包文件校验失败")
        track_ids = [t["trackId"] for t in manifest["tracks"]]
        if len(track_ids) != len(set(track_ids)) or any(not isinstance(t["trackId"], int) or t["trackId"] <= 0 or not isinstance(t["locationId"], int) or t["locationId"] <= 0 for t in manifest["tracks"]):
            raise ValueError("歌曲标识无效")
        if any(t["audioPath"] and roles.get(t["audioPath"]) != "audio" for t in manifest["tracks"]):
            raise ValueError("歌曲关联不存在的文件")
        if any(c["trackId"] not in track_ids or roles.get(c["coverPath"]) != "artwork" for c in manifest.get("covers", [])):
            raise ValueError("封面关联无效")
        return manifest
