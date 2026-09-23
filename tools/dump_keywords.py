#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Skyrim KWDA Analyzer - GUI Version (Batch + Deduplication + Debug)
Analyzes KWDA (Keywords) in Skyrim plugins and resolves them to KYWD EDIDs.
Supports single file, recursive folder scanning, and deduplication.
"""

import json
import struct
import sys
import zlib
import os
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, Iterator, List, Optional, Tuple, Set
import tkinter as tk
from tkinter import ttk, filedialog, messagebox, scrolledtext
import threading

HEADER_SIZE = 24
FLAG_COMPRESSED = 0x00040000

def u16(b: bytes, off: int) -> int:
    return struct.unpack_from("<H", b, off)[0]

def u32(b: bytes, off: int) -> int:
    return struct.unpack_from("<I", b, off)[0]

def i32(b: bytes, off: int) -> int:
    return struct.unpack_from("<i", b, off)[0]

@dataclass
class RecordHeader:
    sig: str
    size: int
    flags: int
    form_id: int

@dataclass
class PluginInfo:
    name: str
    path: Path
    masters: List[str]
    kywd_by_object: Dict[int, str]

def parse_record_header(b: bytes, off: int) -> RecordHeader:
    sig = b[off:off+4].decode("ascii", "replace")
    size = u32(b, off + 4)
    flags = u32(b, off + 8)
    form_id = u32(b, off + 12)
    return RecordHeader(sig=sig, size=size, flags=flags, form_id=form_id)

def parse_group_header(b: bytes, off: int) -> Tuple[int, bytes, int]:
    size = u32(b, off + 4)
    label = b[off + 8: off + 12]
    group_type = i32(b, off + 12)
    return size, label, group_type

def iter_records(plugin_bytes: bytes) -> Iterator[Tuple[RecordHeader, bytes]]:
    if len(plugin_bytes) < HEADER_SIZE:
        return

    rh0 = parse_record_header(plugin_bytes, 0)
    if rh0.sig == "TES4":
        start = HEADER_SIZE + rh0.size
    else:
        start = 0

    def walk(off: int, length: int) -> Iterator[Tuple[RecordHeader, bytes]]:
        end = off + length
        p = off
        while p + 4 <= end and p + HEADER_SIZE <= len(plugin_bytes):
            sig = plugin_bytes[p:p+4]
            if sig == b"GRUP":
                if p + HEADER_SIZE > len(plugin_bytes):
                    return
                gsize, _label, _gtype = parse_group_header(plugin_bytes, p)
                if gsize < HEADER_SIZE:
                    return
                inner_off = p + HEADER_SIZE
                inner_len = gsize - HEADER_SIZE
                yield from walk(inner_off, inner_len)
                p += gsize
                continue

            rec = parse_record_header(plugin_bytes, p)
            data_off = p + HEADER_SIZE
            data_end = data_off + rec.size
            if data_end > len(plugin_bytes):
                return
            yield rec, plugin_bytes[data_off:data_end]
            p = data_end

    yield from walk(start, len(plugin_bytes) - start)

def iter_fields(record_data: bytes, compressed: bool) -> Iterator[Tuple[str, bytes]]:
    data = record_data
    if compressed and len(data) >= 6:
        try:
            data = zlib.decompress(data[4:])
        except Exception:
            data = record_data

    p = 0
    large_size: Optional[int] = None

    while p + 6 <= len(data):
        ftype = data[p:p+4].decode("ascii", "replace")
        fsize = u16(data, p + 4)
        p += 6

        if ftype == "XXXX":
            if p + 4 > len(data):
                return
            large_size = u32(data, p)
            p += 4
            continue

        size = large_size if large_size is not None else fsize
        large_size = None

        if p + size > len(data):
            return

        payload = data[p:p+size]
        p += size
        yield ftype, payload

def parse_tes4_masters(plugin_bytes: bytes) -> List[str]:
    if len(plugin_bytes) < HEADER_SIZE:
        return []
    rh = parse_record_header(plugin_bytes, 0)
    if rh.sig != "TES4":
        return []

    data = plugin_bytes[HEADER_SIZE:HEADER_SIZE + rh.size]
    masters: List[str] = []
    for ftype, payload in iter_fields(data, compressed=(rh.flags & FLAG_COMPRESSED) != 0):
        if ftype == "MAST":
            s = payload.split(b"\x00", 1)[0].decode("utf-8", "replace")
            if s:
                masters.append(s)
    return masters

def parse_edid(record_data: bytes, compressed: bool) -> Optional[str]:
    for ftype, payload in iter_fields(record_data, compressed):
        if ftype == "EDID":
            return payload.split(b"\x00", 1)[0].decode("utf-8", "replace")
    return None

def parse_kwda_formids(record_data: bytes, compressed: bool) -> List[int]:
    for ftype, payload in iter_fields(record_data, compressed):
        if ftype == "KWDA":
            if len(payload) % 4 != 0:
                return []
            return list(struct.unpack("<" + "I" * (len(payload) // 4), payload))
    return []

def find_plugin_path(name_or_path: str, search_dirs: List[Path]) -> Optional[Path]:
    p = Path(name_or_path)
    if p.exists():
        return p.resolve()

    for d in search_dirs:
        cand = d / name_or_path
        if cand.exists():
            return cand.resolve()
        if d.exists():
            lower = name_or_path.lower()
            for child in d.iterdir():
                if child.name.lower() == lower:
                    return child.resolve()
    return None

def resolve_kw_formid_to_master(formid: int, current: PluginInfo) -> Tuple[Optional[str], int]:
    mod_index = (formid >> 24) & 0xFF
    object_id = formid & 0x00FFFFFF

    if mod_index == 0xFE:
        return None, object_id

    if mod_index == 0:
        return current.name, object_id

    mi = mod_index - 1
    if 0 <= mi < len(current.masters):
        return current.masters[mi], object_id

    return None, object_id

def build_dependency_graph(target_path: Path, data_dir: Optional[Path], cache: Optional[Dict[str, PluginInfo]] = None) -> Dict[str, PluginInfo]:
    loaded: Dict[str, PluginInfo] = cache if cache is not None else {}

    search_dirs = [target_path.parent]
    if data_dir:
        search_dirs.append(data_dir)

    def load_one(name_or_path: str) -> None:
        path = find_plugin_path(name_or_path, search_dirs)
        if path is None:
            return
        name = path.name
        key = name.lower()
        if key in loaded:
            return

        try:
            b = path.read_bytes()
        except Exception:
            return

        masters = parse_tes4_masters(b)
        info = PluginInfo(name=name, path=path, masters=masters, kywd_by_object={})
        
        for rh, rdata in iter_records(b):
            if rh.sig != "KYWD":
                continue
            edid = parse_edid(rdata, compressed=(rh.flags & FLAG_COMPRESSED) != 0)
            if not edid:
                continue
            object_id = rh.form_id & 0x00FFFFFF
            info.kywd_by_object[object_id] = edid
            
        loaded[key] = info

        for m in masters:
            load_one(m)

    load_one(str(target_path))
    return loaded


class KWDAAnalyzerGUI:
    def __init__(self, root):
        self.root = root
        self.root.title("Skyrim KWDA Analyzer Pro (v2.1)")
        self.root.geometry("950x800")
        
        self.input_path = tk.StringVar()
        self.data_path = tk.StringVar()
        self.sig_filter = tk.StringVar(value="WEAP,AMMO,ARMO")
        self.record_filter = tk.StringVar()
        self.kw_filter = tk.StringVar()
        self.output_format = tk.StringVar(value="TSV")
        self.is_batch = tk.BooleanVar(value=False)
        self.remove_dupes = tk.BooleanVar(value=True)
        self.show_debug = tk.BooleanVar(value=False)
        
        self.create_widgets()
        
    def create_widgets(self):
        # Main frame
        main_frame = ttk.Frame(self.root, padding="10")
        main_frame.grid(row=0, column=0, sticky=(tk.W, tk.E, tk.N, tk.S))
        
        # Input selection
        input_frame = ttk.Frame(main_frame)
        input_frame.grid(row=0, column=0, columnspan=3, sticky=(tk.W, tk.E), pady=5)
        
        self.input_label = ttk.Label(input_frame, text="Plugin File:")
        self.input_label.pack(side=tk.LEFT, padx=(0, 5))
        
        ttk.Entry(input_frame, textvariable=self.input_path, width=50).pack(side=tk.LEFT, padx=5, fill=tk.X, expand=True)
        ttk.Button(input_frame, text="Browse", command=self.browse_input).pack(side=tk.LEFT, padx=5)
        
        # Options frame (Batch & Dupes)
        opts_frame = ttk.Frame(main_frame)
        opts_frame.grid(row=1, column=0, columnspan=3, sticky=tk.W, pady=2)
        
        ttk.Checkbutton(opts_frame, text="Klasör Tara (Batch Mode)", variable=self.is_batch, command=self.toggle_mode).pack(side=tk.LEFT, padx=(0, 20))
        ttk.Checkbutton(opts_frame, text="Tekrarlananları Gizle (Remove Duplicates)", variable=self.remove_dupes).pack(side=tk.LEFT, padx=(0, 20))
        ttk.Checkbutton(opts_frame, text="Debug Mode", variable=self.show_debug).pack(side=tk.LEFT)
        
        # Data folder
        ttk.Label(main_frame, text="Skyrim Data Folder:").grid(row=2, column=0, sticky=tk.W, pady=5)
        ttk.Entry(main_frame, textvariable=self.data_path, width=60).grid(row=2, column=1, padx=5, sticky=(tk.W, tk.E))
        ttk.Button(main_frame, text="Browse", command=self.browse_data).grid(row=2, column=2)
        
        # Filters frame
        filter_frame = ttk.LabelFrame(main_frame, text="Filters", padding="10")
        filter_frame.grid(row=3, column=0, columnspan=3, sticky=(tk.W, tk.E), pady=10)
        
        ttk.Label(filter_frame, text="Record Signatures:").grid(row=0, column=0, sticky=tk.W, pady=5)
        ttk.Entry(filter_frame, textvariable=self.sig_filter, width=40).grid(row=0, column=1, padx=5, sticky=tk.W)
        ttk.Label(filter_frame, text="(comma-separated, e.g., WEAP,AMMO or ALL)").grid(row=0, column=2, sticky=tk.W)
        
        ttk.Label(filter_frame, text="Record EDID Filter:").grid(row=1, column=0, sticky=tk.W, pady=5)
        ttk.Entry(filter_frame, textvariable=self.record_filter, width=40).grid(row=1, column=1, padx=5, sticky=tk.W)
        ttk.Label(filter_frame, text="(substring match)").grid(row=1, column=2, sticky=tk.W)
        
        ttk.Label(filter_frame, text="Keyword Filter:").grid(row=2, column=0, sticky=tk.W, pady=5)
        ttk.Entry(filter_frame, textvariable=self.kw_filter, width=40).grid(row=2, column=1, padx=5, sticky=tk.W)
        ttk.Label(filter_frame, text="(substring match)").grid(row=2, column=2, sticky=tk.W)
        
        # Output format
        output_frame = ttk.Frame(main_frame)
        output_frame.grid(row=4, column=0, columnspan=3, pady=5)
        
        ttk.Label(output_frame, text="Output Format:").pack(side=tk.LEFT, padx=5)
        ttk.Radiobutton(output_frame, text="TSV", variable=self.output_format, value="TSV").pack(side=tk.LEFT, padx=5)
        ttk.Radiobutton(output_frame, text="JSONL", variable=self.output_format, value="JSONL").pack(side=tk.LEFT, padx=5)
        ttk.Radiobutton(output_frame, text="INI Mapping", variable=self.output_format, value="INI").pack(side=tk.LEFT, padx=5)
        
        # Analyze button
        self.btn_analyze = ttk.Button(main_frame, text="Analyze", command=self.analyze, style="Accent.TButton")
        self.btn_analyze.grid(row=5, column=0, columnspan=3, pady=10)
        
        # Progress bar
        self.progress = ttk.Progressbar(main_frame, mode='determinate')
        self.progress.grid(row=6, column=0, columnspan=3, sticky=(tk.W, tk.E), pady=5)
        
        # Status label
        self.status_label = ttk.Label(main_frame, text="Ready", foreground="green")
        self.status_label.grid(row=7, column=0, columnspan=3, pady=5)
        
        # Results text area
        results_frame = ttk.LabelFrame(main_frame, text="Results", padding="5")
        results_frame.grid(row=8, column=0, columnspan=3, sticky=(tk.W, tk.E, tk.N, tk.S), pady=10)
        
        self.results_text = scrolledtext.ScrolledText(results_frame, width=100, height=20, wrap=tk.WORD)
        self.results_text.pack(fill=tk.BOTH, expand=True)
        
        # Buttons frame
        button_frame = ttk.Frame(main_frame)
        button_frame.grid(row=9, column=0, columnspan=3, pady=5)
        
        ttk.Button(button_frame, text="Copy Results", command=self.copy_results).pack(side=tk.LEFT, padx=5)
        ttk.Button(button_frame, text="Save to File", command=self.save_results).pack(side=tk.LEFT, padx=5)
        ttk.Button(button_frame, text="Clear", command=self.clear_results).pack(side=tk.LEFT, padx=5)
        
        # Configure grid weights
        self.root.columnconfigure(0, weight=1)
        self.root.rowconfigure(0, weight=1)
        main_frame.columnconfigure(1, weight=1)
        main_frame.rowconfigure(8, weight=1)

    def toggle_mode(self):
        if self.is_batch.get():
            self.input_label.config(text="Scan Folder:")
        else:
            self.input_label.config(text="Plugin File:")

    def browse_input(self):
        if self.is_batch.get():
            dirname = filedialog.askdirectory(title="Select Folder to Scan")
            if dirname:
                self.input_path.set(dirname)
        else:
            filename = filedialog.askopenfilename(
                title="Select Plugin File",
                filetypes=[("Skyrim Plugins", "*.esp *.esm *.esl"), ("All Files", "*.*")]
            )
            if filename:
                self.input_path.set(filename)
            
    def browse_data(self):
        dirname = filedialog.askdirectory(title="Select Skyrim Data Folder")
        if dirname:
            self.data_path.set(dirname)
            
    def analyze(self):
        if not self.input_path.get():
            messagebox.showerror("Error", "Please select a file or folder")
            return
            
        self.btn_analyze.config(state="disabled")
        thread = threading.Thread(target=self.run_analysis, daemon=True)
        thread.start()
        
    def run_analysis(self):
        self.root.after(0, self.progress.start)
        self.root.after(0, lambda: self.status_label.config(text="Initializing...", foreground="blue"))
        self.root.after(0, self.clear_results)
        
        debug_log = []
        
        try:
            input_p = Path(self.input_path.get()).resolve()
            data_dir = Path(self.data_path.get()).resolve() if self.data_path.get() else None
            
            targets = []
            if self.is_batch.get():
                if not input_p.is_dir():
                    raise ValueError("Selected path is not a directory")
                
                # Find all plugin files recursively
                debug_log.append(f"# Scanning folder: {input_p}")
                for file_path in input_p.rglob("*"):
                    if file_path.is_file() and file_path.suffix.lower() in ['.esp', '.esm', '.esl']:
                        targets.append(file_path)
                        debug_log.append(f"# Found: {file_path.relative_to(input_p)}")
                
                targets = sorted(list(set(targets)), key=lambda p: p.name.lower())
                debug_log.append(f"# Total plugins found: {len(targets)}")
                debug_log.append("")
            else:
                if not input_p.is_file():
                    raise ValueError("Selected path is not a file")
                targets = [input_p]
                debug_log.append(f"# Single file mode: {input_p.name}")
                debug_log.append("")

            if not targets:
                raise ValueError("No plugin files found.")

            total_files = len(targets)
            sigs_raw = [s.strip().upper() for s in self.sig_filter.get().split(",") if s.strip()]
            scan_all = (len(sigs_raw) == 1 and sigs_raw[0] == "ALL")
            sigs = set(sigs_raw) if not scan_all else set()
            rec_filter = self.record_filter.get().lower() if self.record_filter.get() else None
            kw_filter = self.kw_filter.get().lower() if self.kw_filter.get() else None
            
            should_dedupe = self.remove_dupes.get()
            show_debug = self.show_debug.get()
            
            # Global deduplication tracking
            seen_outputs = set()
            results = []
            
            shared_cache: Dict[str, PluginInfo] = {}
            
            self.root.after(0, self.progress.stop)
            self.root.after(0, lambda: self.progress.config(mode='determinate', maximum=total_files, value=0))

            # Per-plugin statistics
            plugin_stats = {}

            for idx, target_path in enumerate(targets):
                msg = f"Analyzing ({idx+1}/{total_files}): {target_path.name}"
                self.root.after(0, lambda m=msg: self.status_label.config(text=m))
                self.root.after(0, lambda v=idx: self.progress.config(value=v))
                
                plugin_record_count = 0
                plugin_kwda_count = 0
                
                try:
                    dep = build_dependency_graph(target_path, data_dir, cache=shared_cache)
                    target_key = target_path.name.lower()
                    if target_key not in dep:
                        debug_log.append(f"# WARNING: Could not load {target_path.name}")
                        continue
                        
                    target_info = dep[target_key]
                    b = target_path.read_bytes()
                    
                    for rh, rdata in iter_records(b):
                        if not scan_all and rh.sig not in sigs:
                            continue
                        
                        plugin_record_count += 1
                            
                        compressed = (rh.flags & FLAG_COMPRESSED) != 0
                        kwda = parse_kwda_formids(rdata, compressed)
                        if not kwda:
                            continue
                        
                        plugin_kwda_count += 1
                            
                        rec_edid = parse_edid(rdata, compressed) or ""
                        if rec_filter and rec_filter not in rec_edid.lower():
                            continue
                            
                        # Resolve Keywords
                        raw_resolved_names = []
                        raw_resolved_formids = []
                        
                        for kfid in kwda:
                            mname, obj = resolve_kw_formid_to_master(kfid, target_info)
                            f_str = f"{kfid:08X}"
                            
                            name_str = ""
                            if mname is None:
                                name_str = f_str
                            else:
                                mi = dep.get(mname.lower())
                                if mi is None:
                                    name_str = f"{mname}:{f_str}"
                                else:
                                    kname = mi.kywd_by_object.get(obj)
                                    name_str = kname if kname else f"{mname}:{f_str}"
                            
                            raw_resolved_names.append(name_str)
                            raw_resolved_formids.append(f_str)

                        # Keyword Level Deduplication (If record has same keyword twice)
                        final_names = []
                        final_formids = []
                        seen_kw_in_record = set()
                        
                        if should_dedupe:
                            for n, f in zip(raw_resolved_names, raw_resolved_formids):
                                if n not in seen_kw_in_record:
                                    seen_kw_in_record.add(n)
                                    final_names.append(n)
                                    final_formids.append(f)
                        else:
                            final_names = raw_resolved_names
                            final_formids = raw_resolved_formids

                        if kw_filter:
                            ok = any(kw_filter in n.lower() for n in final_names)
                            if not ok:
                                continue

                        # Generate Output String
                        output_line = ""
                        
                        if self.output_format.get() == "JSONL":
                            obj = {
                                "file": target_path.name,
                                "sig": rh.sig,
                                "record_formid": f"{rh.form_id:08X}",
                                "record_edid": rec_edid,
                                "keywords": [{"formid": f, "name": n} for f, n in zip(final_formids, final_names)],
                            }
                            output_line = json.dumps(obj, ensure_ascii=False)
                        elif self.output_format.get() == "INI":
                            formid_short = f"{rh.form_id & 0xFFFFFF:06X}"
                            kw_list = ", ".join(final_names)
                            block = []
                            block.append(f"; [{target_path.name}] {rec_edid} - {rh.sig} [{rh.form_id:08X}]")
                            block.append(f"; Keywords: {kw_list}")
                            block.append(f"{formid_short} = YourPresetNameHere  ; {rec_edid}")
                            block.append("")
                            output_line = "\n".join(block)
                        else:
                            # TSV
                            output_line = (
                                f"{target_path.name}\t{rh.sig}\t{rh.form_id:08X}\t{rec_edid}\t"
                                f"{', '.join(final_names)}\t{', '.join(final_formids)}"
                            )
                        
                        # Result List Level Deduplication
                        if should_dedupe:
                            if output_line in seen_outputs:
                                continue
                            seen_outputs.add(output_line)
                        
                        results.append(output_line)
                    
                    plugin_stats[target_path.name] = {
                        'records': plugin_record_count,
                        'with_kwda': plugin_kwda_count
                    }

                except Exception as e:
                    debug_log.append(f"# ERROR: {target_path.name} - {str(e)}")
                    print(f"Error processing {target_path.name}: {e}")
                    import traceback
                    traceback.print_exc()
                    continue

            # Add debug info if enabled
            if show_debug:
                debug_output = "\n".join(debug_log)
                if self.output_format.get() == "INI":
                    debug_output += "\n\n; ===== PER-PLUGIN STATISTICS =====\n"
                    for pname, stats in plugin_stats.items():
                        debug_output += f"; {pname}: {stats['records']} records scanned, {stats['with_kwda']} with KWDA\n"
                    debug_output += "\n"
                else:
                    debug_output += "\n\n# ===== PER-PLUGIN STATISTICS =====\n"
                    for pname, stats in plugin_stats.items():
                        debug_output += f"# {pname}: {stats['records']} records scanned, {stats['with_kwda']} with KWDA\n"
                    debug_output += "\n"
                
                output = debug_output + "\n".join(results)
            else:
                output = "\n".join(results)
            
            self.root.after(0, lambda: self.results_text.insert(1.0, output))
            
            count_msg = f"Complete! Found {len(results)} unique records from {total_files} plugins." if should_dedupe else f"Complete! Found {len(results)} records from {total_files} plugins."
            self.root.after(0, lambda: self.status_label.config(
                text=count_msg, 
                foreground="green"
            ))
            self.root.after(0, lambda: self.progress.config(value=total_files))
            
        except Exception as e:
            self.root.after(0, lambda: messagebox.showerror("Error", str(e)))
            self.root.after(0, lambda: self.status_label.config(text="Error occurred", foreground="red"))
            import traceback
            traceback.print_exc()
            
        finally:
            self.root.after(0, lambda: self.btn_analyze.config(state="normal"))
            self.root.after(0, self.progress.stop)
            
    def copy_results(self):
        text = self.results_text.get(1.0, tk.END)
        self.root.clipboard_clear()
        self.root.clipboard_append(text)
        messagebox.showinfo("Success", "Results copied to clipboard")
        
    def save_results(self):
        text = self.results_text.get(1.0, tk.END)
        if not text.strip():
            messagebox.showwarning("Warning", "No results to save")
            return
            
        fmt = self.output_format.get()
        if fmt == "INI":
            ext = ".ini"
            filetypes = [("INI Files", "*.ini"), ("All Files", "*.*")]
        elif fmt == "JSONL":
            ext = ".jsonl"
            filetypes = [("JSONL Files", "*.jsonl"), ("All Files", "*.*")]
        else:
            ext = ".tsv"
            filetypes = [("TSV Files", "*.tsv"), ("All Files", "*.*")]
            
        filename = filedialog.asksaveasfilename(
            defaultextension=ext,
            filetypes=filetypes
        )
        if filename:
            with open(filename, "w", encoding="utf-8") as f:
                f.write(text)
            messagebox.showinfo("Success", f"Results saved to {filename}")
            
    def clear_results(self):
        self.results_text.delete(1.0, tk.END)


def main():
    root = tk.Tk()
    app = KWDAAnalyzerGUI(root)
    root.mainloop()


if __name__ == "__main__":
    main()