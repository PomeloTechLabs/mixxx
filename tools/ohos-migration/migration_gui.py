import os
import json
from pathlib import Path
import queue
import threading
import time
import sys
import tkinter as tk
from tkinter import filedialog, messagebox, ttk

from migration_core import Cancelled, Source


def find_local_config_directory():
    candidates = [Path(os.environ[key]) / "Mixxx" for key in ("LOCALAPPDATA", "APPDATA") if os.environ.get(key)]
    candidates.extend([Path.home() / "AppData/Local/Mixxx", Path.home() / ".mixxx"])
    for candidate in candidates:
        if (candidate / "mixxx.cfg").is_file() and (candidate / "mixxxdb.sqlite").is_file():
            return candidate
    return None


class App:
    def __init__(self, window):
        self.window = window
        self.source = None
        self.events = queue.Queue()
        self.cancel = threading.Event()
        self.busy = False
        self.root_vars = {}
        self.default_directory = find_local_config_directory()
        self.mapping_file = Path(os.getenv("LOCALAPPDATA", Path.home())) / "PomeloMixxxMigration" / "mappings.json"
        window.title("旧柚Mixxx迁移助手")
        window.geometry("1000x700")
        window.minsize(760, 560)
        window.protocol("WM_DELETE_WINDOW", self.close)
        main = ttk.Frame(window, padding=16)
        main.pack(fill="both", expand=True)
        top = ttk.Frame(main)
        top.pack(fill="x")
        ttk.Button(top, text="读取本机 Mixxx 配置", command=self.choose_local_directory,
                state="normal" if self.default_directory else "disabled").pack(side="left")
        ttk.Button(top, text="选择其他配置目录", command=self.choose_directory).pack(side="left", padx=8)
        ttk.Button(top, text="从备份文件读取…", command=self.choose_archive).pack(side="right")
        self.status = tk.StringVar(value="请先关闭 Windows Mixxx，再直接读取本机配置目录，并检查音乐位置。无需先压缩备份。")
        ttk.Label(main, textvariable=self.status, wraplength=950).pack(fill="x", pady=10)
        directory_hint = str(self.default_directory) if self.default_directory else "未找到默认配置；可选择自定义的 Mixxx 配置目录。"
        ttk.Label(main, text="本机配置：" + directory_hint, wraplength=950).pack(fill="x", pady=(0, 8))
        self.roots_frame = ttk.LabelFrame(main, text="音乐位置：选择每个原目录现在对应的文件夹", padding=8)
        self.roots_frame.pack(fill="x")
        self.roots_canvas = tk.Canvas(self.roots_frame, height=145, highlightthickness=0)
        roots_scroll = ttk.Scrollbar(self.roots_frame, orient="vertical", command=self.roots_canvas.yview)
        self.roots_canvas.configure(yscrollcommand=roots_scroll.set)
        roots_scroll.pack(side="right", fill="y")
        self.roots_canvas.pack(side="left", fill="x", expand=True)
        self.roots_content = ttk.Frame(self.roots_canvas)
        roots_window = self.roots_canvas.create_window((0, 0), window=self.roots_content, anchor="nw")
        self.roots_content.bind("<Configure>", lambda e: self.roots_canvas.configure(scrollregion=self.roots_canvas.bbox("all")))
        self.roots_canvas.bind("<Configure>", lambda e: self.roots_canvas.itemconfigure(roots_window, width=e.width))
        middle = ttk.Frame(main)
        middle.pack(fill="both", expand=True, pady=10)
        self.table = ttk.Treeview(middle, columns=("state", "original", "current"), show="headings")
        for key, title, width in (("state", "状态", 95), ("original", "原音乐路径", 410), ("current", "现在的音乐位置", 400)):
            self.table.heading(key, text=title)
            self.table.column(key, width=width, minwidth=70)
        scroll = ttk.Scrollbar(middle, orient="vertical", command=self.table.yview)
        horizontal = ttk.Scrollbar(middle, orient="horizontal", command=self.table.xview)
        self.table.configure(yscrollcommand=scroll.set, xscrollcommand=horizontal.set)
        self.table.grid(row=0, column=0, sticky="nsew")
        scroll.grid(row=0, column=1, sticky="ns")
        horizontal.grid(row=1, column=0, sticky="ew")
        middle.rowconfigure(0, weight=1)
        middle.columnconfigure(0, weight=1)
        bottom = ttk.Frame(main)
        bottom.pack(fill="x")
        ttk.Button(bottom, text="为选中歌曲指定文件", command=self.choose_track).pack(side="left")
        self.music = tk.BooleanVar(value=True)
        self.cache = tk.BooleanVar(value=False)
        ttk.Checkbutton(bottom, text="携带音乐", variable=self.music).pack(side="left", padx=10)
        ttk.Checkbutton(bottom, text="携带波形缓存", variable=self.cache).pack(side="left")
        self.generate = ttk.Button(bottom, text="生成迁移包", command=self.start_package)
        self.generate.pack(side="right")
        self.cancel_button = ttk.Button(bottom, text="取消", command=self.cancel.set, state="disabled")
        self.cancel_button.pack(side="right", padx=8)
        self.progress = ttk.Progressbar(main, maximum=100)
        self.progress.pack(fill="x", pady=(12, 0))
        window.after(100, self.poll)

    def choose_archive(self):
        if not self.busy:
            path = filedialog.askopenfilename(filetypes=[("Mixxx 备份", "*.rar *.zip *.cfg"), ("所有文件", "*.*")])
            if path:
                self.load(path)

    def choose_directory(self):
        if not self.busy:
            path = filedialog.askdirectory(title="选择含 mixxx.cfg 和 mixxxdb.sqlite 的配置目录",
                    initialdir=str(self.default_directory or Path.home()))
            if path:
                self.load(path)

    def choose_local_directory(self):
        if not self.busy and self.default_directory:
            self.load(self.default_directory)

    def set_busy(self, busy):
        self.busy = busy
        self.generate.configure(state="disabled" if busy else "normal")
        self.cancel_button.configure(state="normal" if busy else "disabled")

    def load(self, path):
        self.set_busy(True)
        self.status.set("正在读取配置并检查曲库…")
        self.cancel_button.configure(state="disabled")
        def worker():
            try:
                self.events.put(("loaded", Source(path)))
            except Exception as e:
                self.events.put(("error", str(e)))
        threading.Thread(target=worker, daemon=True).start()

    def populate(self, source):
        if self.source:
            self.source.close()
        self.source = source
        for child in self.roots_content.winfo_children():
            child.destroy()
        try:
            previous = json.loads(self.mapping_file.read_text(encoding="utf-8"))
            source.mapping.update({r: p for r, p in previous.items() if r in source.roots and Path(p).is_dir()})
        except (OSError, ValueError):
            pass
        self.root_vars = {}
        for row, old in enumerate(source.roots):
            ttk.Label(self.roots_content, text=old, width=36).grid(row=row, column=0, sticky="w", pady=3)
            var = tk.StringVar(value=source.mapping.get(old, ""))
            self.root_vars[old] = var
            ttk.Entry(self.roots_content, textvariable=var, state="readonly").grid(row=row, column=1, sticky="ew", padx=8)
            ttk.Button(self.roots_content, text="选择文件夹", command=lambda r=old: self.choose_root(r)).grid(row=row, column=2)
        self.roots_content.columnconfigure(1, weight=1)
        source.match(source.mapping)
        self.refresh()

    def choose_root(self, old):
        if self.busy:
            return
        path = filedialog.askdirectory()
        if path:
            self.root_vars[old].set(path)
            self.source.match({r: v.get() for r, v in self.root_vars.items() if v.get()})
            self.mapping_file.parent.mkdir(parents=True, exist_ok=True)
            self.mapping_file.write_text(json.dumps(self.source.mapping, ensure_ascii=False), encoding="utf-8")
            self.refresh()

    def choose_track(self):
        if self.busy or not self.source or not self.table.selection():
            return
        path = filedialog.askopenfilename(title="选择该歌曲的实际音频文件")
        if path:
            try:
                self.source.choose_file(int(self.table.selection()[0]), path)
                self.refresh()
            except Exception as e:
                messagebox.showerror("无法关联", str(e))

    def refresh(self):
        self.table.delete(*self.table.get_children())
        labels = {"matched": "已找到", "missing": "缺失", "changed": "文件有变化", "confirmed": "已手动指定"}
        for t in sorted(self.source.tracks, key=lambda t: (t["status"] == "matched", t["original"])):
            self.table.insert("", "end", iid=str(t["trackId"]), values=(labels[t["status"]], t["original"], t["resolved"] or ""))
        found = sum(t["status"] in ("matched", "confirmed") for t in self.source.tracks)
        size = sum(Path(t["resolved"]).stat().st_size for t in self.source.tracks if t["status"] in ("matched", "confirmed"))
        self.status.set(f"曲库 {len(self.source.tracks)} 首，已找到/确认 {found} 首，音乐约 {size / 1024 ** 3:.2f} GB；CUE {getattr(self.source, 'cue_count', 0)} 条。请核对未关联歌曲。")

    def start_package(self):
        if self.busy or not self.source:
            return
        absent = sum(t["status"] not in ("matched", "confirmed") for t in self.source.tracks)
        if self.music.get() and absent and not messagebox.askyesno("仍有歌曲未确认", f"有 {absent} 首缺失或文件发生变化。继续将生成带缺失记录的迁移包，保留这些歌曲的 CUE 和歌单。是否继续？"):
            return
        output = filedialog.asksaveasfilename(title="保存迁移包", initialfile="Mixxx迁移包.zip", defaultextension=".zip", filetypes=[("迁移包", "*.zip")])
        if not output:
            return
        self.cancel.clear()
        self.set_busy(True)
        self.status.set("正在打包，完成后将校验文件…")
        music, cache = self.music.get(), self.cache.get()
        def worker():
            last = 0
            def progress(done, total, name):
                nonlocal last
                now = time.monotonic()
                if now - last > 0.15:
                    self.events.put(("progress", (done, total, name)))
                    last = now
            try:
                manifest = self.source.create_package(output, music, cache, progress, self.cancel)
                self.events.put(("complete", (output, manifest)))
            except Cancelled:
                self.events.put(("cancelled", None))
            except Exception as e:
                self.events.put(("error", str(e)))
        threading.Thread(target=worker, daemon=True).start()

    def poll(self):
        while not self.events.empty():
            event, value = self.events.get()
            if event == "loaded":
                self.set_busy(False)
                self.populate(value)
            elif event == "progress":
                done, total, name = value
                self.progress["value"] = 100 * done / max(1, total)
                self.status.set(f"正在处理 {name}（{done / 1024 ** 2:.0f} / {total / 1024 ** 2:.0f} MB）；随后校验迁移包")
            elif event == "complete":
                self.set_busy(False)
                self.progress["value"] = 100
                output, manifest = value
                self.status.set("迁移包已生成并通过文件校验：" + output)
                messagebox.showinfo("完成", f"已携带 {manifest['includedTracks']} 首音乐，未携带 {manifest['missingTracks']} 首。\n复制到手机/Pad 后，在 Mixxx 的选项菜单选择“导入迁移包”。")
                if messagebox.askyesno("打开输出目录", "是否打开迁移包所在文件夹？"):
                    os.startfile(str(Path(output).parent))
            elif event == "cancelled":
                self.set_busy(False)
                self.status.set("已取消打包")
            elif event == "error":
                self.set_busy(False)
                messagebox.showerror("操作失败", value)
                self.status.set("操作失败；请选择资料或调整音乐关联后重试")
        self.window.after(100, self.poll)

    def close(self):
        if self.busy:
            self.cancel.set()
            messagebox.showinfo("正在停止", "已请求停止，请等待当前读取或校验完成后关闭。")
            return
        if self.source:
            self.source.close()
        self.window.destroy()


if __name__ == "__main__":
    if len(sys.argv) == 4 and sys.argv[1] == "--inspect":
        source = Source(sys.argv[2])
        try:
            report = {"tracks": len(source.tracks), "roots": len(source.roots), "cues": getattr(source, "cue_count", 0), "playlists": getattr(source, "playlist_count", 0), "schema": source.schema}
            Path(sys.argv[3]).write_text(json.dumps(report, ensure_ascii=False), encoding="utf-8")
        finally:
            source.close()
        sys.exit(0)
    if os.name == "nt":
        import ctypes
        ctypes.windll.shcore.SetProcessDpiAwareness(1)
    root = tk.Tk()
    App(root)
    root.mainloop()
