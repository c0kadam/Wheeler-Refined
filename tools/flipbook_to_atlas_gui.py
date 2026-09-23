#!/usr/bin/env python3
"""
GUI wrapper for tools/flipbook_to_atlas.py.

Converts numbered PNG sequences to atlas textures for AmmoWheel IsAtlas workflow.
"""

from __future__ import annotations

import re
import sys
import tkinter as tk
from pathlib import Path
from tkinter import filedialog, messagebox, scrolledtext, ttk

THIS_DIR = Path(__file__).resolve().parent
if str(THIS_DIR) not in sys.path:
    sys.path.insert(0, str(THIS_DIR))

import flipbook_to_atlas as core


TARGET_PREFIXES = [
    "PopupBubble",
    "Popup",
    "SlotBackground",
    "SlotFrame",
    "SlotIcon",
    "IndicatorSelected",
    "IndicatorHovered",
    "IndicatorActive",
    "IndicatorCharge",
    "WheelBackground",
    "WheelBorderRing",
    "CenterBackground",
    "CenterPanelFrame",
    "LowAmmoIndicator",
]


PRESET_CUSTOM_LABEL = "Custom"


def derive_glob_from_example(example_file: Path) -> str:
    stem = example_file.stem
    suffix = example_file.suffix
    # fire_burst_00 -> fire_burst_*
    candidate = re.sub(r"\d+$", "*", stem)
    if candidate == stem:
        # no trailing digits found
        candidate = stem + "*"
    return str(example_file.with_name(candidate + suffix))


class AtlasBuilderGUI:
    def __init__(self, root: tk.Tk):
        self.root = root
        self.root.title("Flipbook to Atlas (AmmoWheel)")
        self.root.geometry("980x760")

        self.input_pattern = tk.StringVar()
        self.output_path = tk.StringVar()
        self.offset = tk.IntVar(value=0)
        self.max_frames = tk.StringVar(value="")
        self.cols = tk.StringVar(value="")
        self.rows = tk.StringVar(value="")
        self.selection_mode = tk.StringVar(value="head")
        self.preset_label = tk.StringVar(value=PRESET_CUSTOM_LABEL)

        self.emit_snippet = tk.BooleanVar(value=True)
        self.ini_target = tk.StringVar(value="PopupBubble")
        self.ini_path = tk.StringVar(value="")
        self.ini_out = tk.StringVar(value="")
        self.fps = tk.DoubleVar(value=24.0)
        self.loop = tk.BooleanVar(value=True)
        self.progress_driven = tk.BooleanVar(value=False)
        self.start_time_mode = tk.StringVar(value="0")
        self.preset_labels = [PRESET_CUSTOM_LABEL]
        self.preset_label_to_key: dict[str, str] = {}
        for key, value in core.ATLAS_PRESETS.items():
            label = f"{key} - {value['label']}"
            self.preset_labels.append(label)
            self.preset_label_to_key[label] = key

        self._build_ui()

    def _build_ui(self) -> None:
        main = ttk.Frame(self.root, padding=10)
        main.pack(fill=tk.BOTH, expand=True)

        # Input pattern
        ttk.Label(main, text="Frame Glob Pattern").grid(row=0, column=0, sticky=tk.W, pady=(0, 4))
        ttk.Entry(main, textvariable=self.input_pattern, width=90).grid(
            row=1, column=0, columnspan=4, sticky=tk.EW, padx=(0, 8)
        )
        ttk.Button(main, text="From Folder", command=self._pick_folder).grid(row=1, column=4, sticky=tk.EW)
        ttk.Button(main, text="From Example", command=self._pick_example).grid(row=1, column=5, sticky=tk.EW, padx=(6, 0))

        # Output path
        ttk.Label(main, text="Output Atlas PNG").grid(row=2, column=0, sticky=tk.W, pady=(12, 4))
        ttk.Entry(main, textvariable=self.output_path, width=90).grid(
            row=3, column=0, columnspan=5, sticky=tk.EW, padx=(0, 8)
        )
        ttk.Button(main, text="Browse", command=self._pick_output).grid(row=3, column=5, sticky=tk.EW)

        # Atlas settings
        atlas_group = ttk.LabelFrame(main, text="Atlas Settings", padding=8)
        atlas_group.grid(row=4, column=0, columnspan=6, sticky=tk.EW, pady=(12, 0))

        ttk.Label(atlas_group, text="Offset").grid(row=0, column=0, sticky=tk.W)
        ttk.Entry(atlas_group, textvariable=self.offset, width=8).grid(row=0, column=1, padx=(4, 16))
        ttk.Label(atlas_group, text="Max Frames").grid(row=0, column=2, sticky=tk.W)
        ttk.Entry(atlas_group, textvariable=self.max_frames, width=8).grid(row=0, column=3, padx=(4, 16))
        ttk.Label(atlas_group, text="Cols").grid(row=0, column=4, sticky=tk.W)
        ttk.Entry(atlas_group, textvariable=self.cols, width=8).grid(row=0, column=5, padx=(4, 16))
        ttk.Label(atlas_group, text="Rows").grid(row=0, column=6, sticky=tk.W)
        ttk.Entry(atlas_group, textvariable=self.rows, width=8).grid(row=0, column=7, padx=(4, 0))
        ttk.Label(atlas_group, text="Selection").grid(row=1, column=6, sticky=tk.W, pady=(8, 0))
        ttk.Combobox(
            atlas_group,
            textvariable=self.selection_mode,
            values=["head", "uniform", "motion"],
            state="readonly",
            width=10,
        ).grid(row=1, column=7, sticky=tk.W, padx=(4, 0), pady=(8, 0))
        ttk.Label(atlas_group, text="Preset").grid(row=1, column=0, sticky=tk.W, pady=(8, 0))
        preset_combo = ttk.Combobox(
            atlas_group,
            textvariable=self.preset_label,
            values=self.preset_labels,
            state="readonly",
            width=48,
        )
        preset_combo.grid(row=1, column=1, columnspan=5, sticky=tk.W, padx=(4, 16), pady=(8, 0))
        preset_combo.bind("<<ComboboxSelected>>", self._on_preset_selected)

        # Snippet settings
        snippet_group = ttk.LabelFrame(main, text="INI Snippet", padding=8)
        snippet_group.grid(row=5, column=0, columnspan=6, sticky=tk.EW, pady=(12, 0))

        ttk.Checkbutton(snippet_group, text="Generate snippet", variable=self.emit_snippet).grid(
            row=0, column=0, sticky=tk.W, columnspan=2
        )
        ttk.Label(snippet_group, text="Target").grid(row=1, column=0, sticky=tk.W, pady=(8, 0))
        ttk.Combobox(
            snippet_group,
            textvariable=self.ini_target,
            values=TARGET_PREFIXES,
            state="readonly",
            width=24,
        ).grid(row=1, column=1, sticky=tk.W, padx=(6, 20), pady=(8, 0))

        ttk.Label(snippet_group, text="INI Path").grid(row=1, column=2, sticky=tk.W, pady=(8, 0))
        ttk.Entry(snippet_group, textvariable=self.ini_path, width=40).grid(
            row=1, column=3, columnspan=2, sticky=tk.EW, padx=(6, 8), pady=(8, 0)
        )

        ttk.Label(snippet_group, text="INI Output").grid(row=2, column=0, sticky=tk.W, pady=(8, 0))
        ttk.Entry(snippet_group, textvariable=self.ini_out, width=58).grid(
            row=2, column=1, columnspan=4, sticky=tk.EW, padx=(6, 8), pady=(8, 0)
        )
        ttk.Button(snippet_group, text="Browse", command=self._pick_snippet_output).grid(row=2, column=5, sticky=tk.EW, pady=(8, 0))

        ttk.Label(snippet_group, text="FPS").grid(row=3, column=0, sticky=tk.W, pady=(8, 0))
        ttk.Entry(snippet_group, textvariable=self.fps, width=8).grid(row=3, column=1, sticky=tk.W, padx=(6, 16), pady=(8, 0))
        ttk.Checkbutton(snippet_group, text="Loop", variable=self.loop).grid(row=3, column=2, sticky=tk.W, pady=(8, 0))
        ttk.Checkbutton(snippet_group, text="Progress Driven", variable=self.progress_driven).grid(
            row=3, column=3, sticky=tk.W, pady=(8, 0)
        )
        ttk.Label(snippet_group, text="StartTimeMode").grid(row=3, column=4, sticky=tk.E, pady=(8, 0))
        ttk.Combobox(
            snippet_group,
            textvariable=self.start_time_mode,
            values=["0", "1", "2"],
            state="readonly",
            width=5,
        ).grid(row=3, column=5, sticky=tk.W, padx=(6, 0), pady=(8, 0))

        # Actions
        actions = ttk.Frame(main)
        actions.grid(row=6, column=0, columnspan=6, sticky=tk.EW, pady=(12, 0))
        ttk.Button(actions, text="Preview (Dry Run)", command=self._preview).pack(side=tk.LEFT)
        ttk.Button(actions, text="Build Atlas", command=self._build).pack(side=tk.LEFT, padx=(8, 0))

        # Log
        ttk.Label(main, text="Log").grid(row=7, column=0, sticky=tk.W, pady=(12, 4))
        self.log = scrolledtext.ScrolledText(main, width=120, height=18)
        self.log.grid(row=8, column=0, columnspan=6, sticky=tk.NSEW)

        for i in range(6):
            main.columnconfigure(i, weight=1)
        main.rowconfigure(8, weight=1)
        snippet_group.columnconfigure(3, weight=1)

    def _log(self, msg: str) -> None:
        self.log.insert(tk.END, msg + "\n")
        self.log.see(tk.END)
        self.root.update_idletasks()

    def _pick_folder(self) -> None:
        folder = filedialog.askdirectory(title="Select frame folder")
        if not folder:
            return
        self.input_pattern.set(str(Path(folder) / "*.png"))
        self._log(f"Input pattern set from folder: {self.input_pattern.get()}")

    def _pick_example(self) -> None:
        file_path = filedialog.askopenfilename(
            title="Select an example frame",
            filetypes=[("PNG", "*.png"), ("All files", "*.*")],
        )
        if not file_path:
            return
        pattern = derive_glob_from_example(Path(file_path))
        self.input_pattern.set(pattern)
        self._log(f"Input pattern derived: {pattern}")

        # auto-suggest output path
        p = Path(pattern)
        stem = p.name.replace("*", "atlas")
        self.output_path.set(str(p.parent / stem))
        self._log(f"Output atlas suggested: {self.output_path.get()}")

    def _pick_output(self) -> None:
        out = filedialog.asksaveasfilename(
            title="Select atlas output path",
            defaultextension=".png",
            filetypes=[("PNG", "*.png"), ("All files", "*.*")],
        )
        if out:
            self.output_path.set(out)

    def _pick_snippet_output(self) -> None:
        out = filedialog.asksaveasfilename(
            title="Select snippet output path",
            defaultextension=".ini",
            filetypes=[("INI", "*.ini"), ("All files", "*.*")],
        )
        if out:
            self.ini_out.set(out)

    def _parse_int_or_none(self, value: str, field_name: str) -> int | None:
        value = value.strip()
        if not value:
            return None
        try:
            parsed = int(value)
        except ValueError as exc:
            raise ValueError(f"{field_name} must be an integer.") from exc
        return parsed

    def _on_preset_selected(self, _event: tk.Event | None = None) -> None:
        selected = self.preset_label.get()
        if selected == PRESET_CUSTOM_LABEL:
            self._log("Preset: Custom")
            return

        preset_key = self.preset_label_to_key.get(selected)
        if not preset_key:
            return
        preset = core.ATLAS_PRESETS[preset_key]

        self.offset.set(int(preset["offset"]))
        self.max_frames.set(str(int(preset["max_frames"])))
        self.cols.set(str(int(preset["cols"])))
        self.rows.set(str(int(preset["rows"])))
        self.selection_mode.set(str(preset.get("selection_mode", "head")))
        self.fps.set(float(preset["fps"]))
        self.loop.set(bool(preset["loop"]))
        self.progress_driven.set(bool(preset["progress_driven"]))
        self.start_time_mode.set(str(int(preset["start_time_mode"])))
        self.ini_target.set(str(preset["ini_target"]))
        self._log(f"Preset applied: {preset_key}")

    def _collect_inputs(self) -> dict:
        pattern = self.input_pattern.get().strip()
        output = self.output_path.get().strip()
        if not pattern:
            raise ValueError("Input pattern is required.")
        if not output:
            raise ValueError("Output atlas path is required.")

        cols = self._parse_int_or_none(self.cols.get(), "Cols")
        rows = self._parse_int_or_none(self.rows.get(), "Rows")
        max_frames = self._parse_int_or_none(self.max_frames.get(), "Max Frames")

        data = {
            "pattern": pattern,
            "output": Path(output),
            "offset": self.offset.get(),
            "max_frames": max_frames,
            "cols": cols,
            "rows": rows,
            "selection_mode": self.selection_mode.get().strip() or "head",
            "emit_snippet": self.emit_snippet.get(),
            "ini_target": self.ini_target.get().strip(),
            "ini_path": self.ini_path.get().strip(),
            "ini_out": self.ini_out.get().strip(),
            "fps": self.fps.get(),
            "loop": self.loop.get(),
            "progress_driven": self.progress_driven.get(),
            "start_time_mode": int(self.start_time_mode.get()),
        }
        return data

    def _run(self, dry_run: bool) -> None:
        try:
            data = self._collect_inputs()
            frames = core.collect_frames(data["pattern"])
            if not frames:
                raise ValueError(f"No frames matched pattern: {data['pattern']}")

            selected = core.select_frames(
                frames,
                data["offset"],
                data["max_frames"],
                data["selection_mode"],
            )
            if not selected:
                raise ValueError("No frames selected after offset/max-frames.")

            cols, rows = core.compute_grid(len(selected), data["cols"], data["rows"])

            self._log("")
            self._log(f"Matched frames: {len(frames)}")
            self._log(
                f"Selected frames: {len(selected)} "
                f"(offset={data['offset']}, max={data['max_frames']}, mode={data['selection_mode']})"
            )
            self._log(f"Grid: {cols} x {rows} (capacity={cols * rows})")
            self._log(f"Output atlas: {data['output']}")

            if dry_run:
                return

            images = core.load_frames(selected)
            atlas = core.build_atlas(images, cols, rows)
            output_path: Path = data["output"]
            output_path.parent.mkdir(parents=True, exist_ok=True)
            atlas.save(output_path, "PNG")
            self._log(f"Saved atlas: {output_path} ({atlas.width}x{atlas.height})")

            if data["emit_snippet"]:
                target = data["ini_target"] or "PopupBubble"
                ini_path = data["ini_path"] or core.default_ini_path_for_output(output_path)
                snippet = core.make_ini_snippet(
                    prefix=target,
                    path_in_ini=ini_path,
                    cols=cols,
                    rows=rows,
                    frame_count=len(selected),
                    fps=float(data["fps"]),
                    loop=bool(data["loop"]),
                    progress_driven=bool(data["progress_driven"]),
                    start_time_mode=int(data["start_time_mode"]),
                )

                if data["ini_out"]:
                    snippet_path = Path(data["ini_out"])
                else:
                    snippet_path = output_path.with_suffix(".snippet.ini")

                snippet_path.parent.mkdir(parents=True, exist_ok=True)
                snippet_path.write_text(snippet, encoding="utf-8")
                self._log(f"Saved snippet: {snippet_path}")

            messagebox.showinfo("Done", "Atlas build completed.")
        except Exception as exc:  # pragma: no cover (GUI error path)
            self._log(f"ERROR: {exc}")
            messagebox.showerror("Error", str(exc))

    def _preview(self) -> None:
        self._run(dry_run=True)

    def _build(self) -> None:
        self._run(dry_run=False)


def main() -> None:
    root = tk.Tk()
    AtlasBuilderGUI(root)
    root.mainloop()


if __name__ == "__main__":
    main()
