#!/usr/bin/env python3
"""
Convert a numbered flipbook PNG sequence into a single atlas texture.

Designed for Wheeler AmmoWheel unified reskin atlas mode:
  <Prefix>_IsAtlas = true
  <Prefix>_AtlasCols / <Prefix>_AtlasRows
"""

from __future__ import annotations

import argparse
import bisect
import glob
import math
import re
import sys
from pathlib import Path
from typing import Iterable, List, Sequence, Tuple

from PIL import Image, ImageChops, ImageStat

if hasattr(Image, "Resampling"):
    RESAMPLE_BILINEAR = Image.Resampling.BILINEAR
else:
    RESAMPLE_BILINEAR = Image.BILINEAR


ATLAS_PRESETS = {
    "popup_bubble_balanced": {
        "label": "PopupBubble Balanced (32 frames, 8x4, 24 FPS)",
        "offset": 0,
        "max_frames": 32,
        "cols": 8,
        "rows": 4,
        "selection_mode": "motion",
        "fps": 24.0,
        "loop": True,
        "progress_driven": False,
        "start_time_mode": 0,
        "ini_target": "PopupBubble",
    },
    "popup_bubble_safe": {
        "label": "PopupBubble Safe (24 frames, 6x4, 20 FPS)",
        "offset": 0,
        "max_frames": 24,
        "cols": 6,
        "rows": 4,
        "selection_mode": "motion",
        "fps": 20.0,
        "loop": True,
        "progress_driven": False,
        "start_time_mode": 0,
        "ini_target": "PopupBubble",
    },
}


def _natural_sort_key(path: Path) -> List[object]:
    parts = re.split(r"(\d+)", path.name)
    key: List[object] = []
    for part in parts:
        if not part:
            continue
        if part.isdigit():
            key.append(int(part))
        else:
            key.append(part.lower())
    return key


def collect_frames(pattern: str) -> List[Path]:
    matches = [Path(p) for p in glob.glob(pattern)]
    frames = [p for p in matches if p.is_file()]
    frames.sort(key=_natural_sort_key)
    return frames


def slice_frames(frames: Sequence[Path], offset: int, max_frames: int | None) -> List[Path]:
    if offset < 0:
        raise ValueError("offset must be >= 0")
    sliced = list(frames[offset:])
    if max_frames is not None:
        if max_frames <= 0:
            raise ValueError("max-frames must be > 0 when provided")
        sliced = sliced[:max_frames]
    return sliced


def _fill_unique_indices(
    indices: Sequence[int],
    total_count: int,
    target_count: int,
    fallback_indices: Sequence[int] | None = None,
) -> List[int]:
    unique: List[int] = []
    seen = set()
    for idx in indices:
        idx = int(max(0, min(total_count - 1, idx)))
        if idx not in seen:
            unique.append(idx)
            seen.add(idx)
        if len(unique) >= target_count:
            return unique

    if len(unique) < target_count and fallback_indices is not None:
        for idx in fallback_indices:
            idx = int(max(0, min(total_count - 1, idx)))
            if idx in seen:
                continue
            unique.append(idx)
            seen.add(idx)
            if len(unique) >= target_count:
                break

    if len(unique) < target_count:
        for idx in range(total_count):
            if idx in seen:
                continue
            unique.append(idx)
            seen.add(idx)
            if len(unique) >= target_count:
                break
    unique.sort()
    return unique[:target_count]


def evenly_spaced_indices(total_count: int, target_count: int) -> List[int]:
    if target_count <= 0:
        raise ValueError("target_count must be > 0")
    if target_count >= total_count:
        return list(range(total_count))
    if target_count == 1:
        return [0]

    indices = [int(i * total_count / target_count) for i in range(target_count)]
    indices[-1] = total_count - 1
    return _fill_unique_indices(indices, total_count, target_count)


def _frame_luma_preview(path: Path, size: int) -> Image.Image:
    with Image.open(path) as im:
        return im.convert("L").resize((size, size), RESAMPLE_BILINEAR).copy()


def motion_weighted_indices(paths: Sequence[Path], target_count: int, preview_size: int = 64) -> List[int]:
    total_count = len(paths)
    if target_count <= 0:
        raise ValueError("target_count must be > 0")
    if target_count >= total_count:
        return list(range(total_count))
    if target_count == 1:
        return [0]

    preview_size = max(16, min(256, int(preview_size)))

    cumulative: List[float] = [0.0]
    prev = _frame_luma_preview(paths[0], preview_size)
    total_motion = 0.0

    for path in paths[1:]:
        cur = _frame_luma_preview(path, preview_size)
        diff = ImageChops.difference(prev, cur)
        score = float(ImageStat.Stat(diff).mean[0])
        total_motion += max(0.0, score)
        cumulative.append(total_motion)
        prev = cur

    if total_motion <= 1e-6:
        return evenly_spaced_indices(total_count, target_count)

    raw_indices: List[int] = []
    for i in range(target_count):
        target = total_motion * (i / float(target_count - 1))
        idx = bisect.bisect_left(cumulative, target)
        if idx >= total_count:
            idx = total_count - 1
        raw_indices.append(idx)

    fallback = evenly_spaced_indices(total_count, target_count)
    return _fill_unique_indices(raw_indices, total_count, target_count, fallback)


def select_frames(
    frames: Sequence[Path],
    offset: int,
    max_frames: int | None,
    selection_mode: str,
) -> List[Path]:
    if offset < 0:
        raise ValueError("offset must be >= 0")
    if max_frames is not None and max_frames <= 0:
        raise ValueError("max-frames must be > 0 when provided")

    pool = list(frames[offset:])
    if max_frames is None or max_frames >= len(pool):
        return pool

    if selection_mode == "head":
        return pool[:max_frames]
    if selection_mode == "uniform":
        idx = evenly_spaced_indices(len(pool), max_frames)
        return [pool[i] for i in idx]
    if selection_mode == "motion":
        idx = motion_weighted_indices(pool, max_frames)
        return [pool[i] for i in idx]
    raise ValueError(f"Unknown selection mode: {selection_mode}")


def compute_grid(frame_count: int, cols: int | None, rows: int | None) -> Tuple[int, int]:
    if frame_count <= 0:
        raise ValueError("frame_count must be > 0")

    if cols is not None and cols <= 0:
        raise ValueError("cols must be > 0")
    if rows is not None and rows <= 0:
        raise ValueError("rows must be > 0")

    if cols is None and rows is None:
        cols = int(math.ceil(math.sqrt(frame_count)))
        rows = int(math.ceil(frame_count / cols))
        return cols, rows

    if cols is not None and rows is not None:
        if cols * rows < frame_count:
            raise ValueError(
                f"grid too small: cols*rows={cols * rows} < frame_count={frame_count}"
            )
        return cols, rows

    if cols is not None:
        rows = int(math.ceil(frame_count / cols))
        return cols, rows

    # rows is not None
    cols = int(math.ceil(frame_count / rows))
    return cols, rows


def load_frames(paths: Iterable[Path]) -> List[Image.Image]:
    images: List[Image.Image] = []
    for path in paths:
        with Image.open(path) as im:
            images.append(im.convert("RGBA").copy())
    return images


def assert_same_dimensions(images: Sequence[Image.Image]) -> Tuple[int, int]:
    if not images:
        raise ValueError("no images loaded")
    w0, h0 = images[0].size
    for idx, im in enumerate(images[1:], start=1):
        if im.size != (w0, h0):
            raise ValueError(
                f"frame size mismatch at index {idx}: {im.size} != {(w0, h0)}"
            )
    return w0, h0


def build_atlas(images: Sequence[Image.Image], cols: int, rows: int) -> Image.Image:
    frame_w, frame_h = assert_same_dimensions(images)
    atlas = Image.new("RGBA", (cols * frame_w, rows * frame_h), (0, 0, 0, 0))
    for index, frame in enumerate(images):
        x = (index % cols) * frame_w
        y = (index // cols) * frame_h
        atlas.paste(frame, (x, y), frame)
    return atlas


def make_ini_snippet(
    prefix: str,
    path_in_ini: str,
    cols: int,
    rows: int,
    frame_count: int,
    fps: float,
    loop: bool,
    progress_driven: bool,
    start_time_mode: int,
) -> str:
    lines = [
        f"; {prefix} atlas animation",
        f"{prefix}_Enabled = true",
        f"{prefix}_IsFlipbook = false",
        f"{prefix}_IsAtlas = true",
        f"{prefix}_Path = {path_in_ini}",
        f"{prefix}_AtlasCols = {cols}",
        f"{prefix}_AtlasRows = {rows}",
        f"{prefix}_FrameCount = {frame_count}",
        f"{prefix}_FPS = {fps}",
        f"{prefix}_Loop = {'true' if loop else 'false'}",
        f"{prefix}_ProgressDriven = {'true' if progress_driven else 'false'}",
        f"{prefix}_StartTimeMode = {start_time_mode}",
    ]
    return "\n".join(lines) + "\n"


def default_ini_path_for_output(output_path: Path) -> str:
    parts = list(output_path.parts)
    lower = [p.lower() for p in parts]
    try:
        idx = lower.index("resources")
        if idx + 1 < len(lower) and lower[idx + 1] == "ammo_wheel":
            rel_parts = parts[idx + 2 :]
            if rel_parts:
                return "/".join(rel_parts).replace("\\", "/")
    except ValueError:
        pass

    parent_name = output_path.parent.name
    if parent_name and parent_name not in (".", ".."):
        return f"{parent_name}/{output_path.name}".replace("\\", "/")
    return output_path.name


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Build an atlas PNG from a flipbook frame sequence."
    )
    parser.add_argument(
        "--input",
        required=True,
        help=r'Input frame glob pattern (example: "Data/.../popup/fire_burst_*.png")',
    )
    parser.add_argument("--output", required=True, help="Output atlas PNG path")
    parser.add_argument("--offset", type=int, default=0, help="Skip first N frames")
    parser.add_argument(
        "--max-frames",
        type=int,
        default=None,
        help="Use at most N frames after offset",
    )
    parser.add_argument("--cols", type=int, default=None, help="Atlas column count")
    parser.add_argument("--rows", type=int, default=None, help="Atlas row count")
    parser.add_argument(
        "--selection-mode",
        choices=["head", "uniform", "motion"],
        default="head",
        help="Frame selection strategy when max-frames is set",
    )
    parser.add_argument(
        "--preset",
        choices=sorted(ATLAS_PRESETS.keys()),
        default=None,
        help="Apply a built-in preset before building atlas/snippet",
    )

    parser.add_argument(
        "--ini-target",
        default=None,
        help="Emit AmmoWheel_Styles snippet for this target prefix (example: PopupBubble)",
    )
    parser.add_argument(
        "--ini-path",
        default=None,
        help="Path value written to <Prefix>_Path in snippet (default: output filename)",
    )
    parser.add_argument(
        "--ini-out",
        default=None,
        help="Snippet output file path (default: <output>.snippet.ini)",
    )
    parser.add_argument("--fps", type=float, default=24.0, help="Snippet FPS value")
    parser.add_argument(
        "--loop",
        dest="loop",
        action="store_true",
        default=True,
        help="Snippet Loop=true (default)",
    )
    parser.add_argument(
        "--no-loop",
        dest="loop",
        action="store_false",
        help="Snippet Loop=false",
    )
    parser.add_argument(
        "--progress-driven",
        action="store_true",
        default=False,
        help="Snippet ProgressDriven=true",
    )
    parser.add_argument(
        "--start-time-mode",
        type=int,
        choices=[0, 1, 2],
        default=0,
        help="Snippet StartTimeMode (0=Global, 1=PerSlot, 2=PerEntry)",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Print plan only, do not write atlas/snippet files",
    )
    return parser.parse_args()


def apply_preset_defaults(args: argparse.Namespace) -> dict | None:
    if not args.preset:
        return None

    preset = ATLAS_PRESETS[args.preset]

    if args.offset == 0:
        args.offset = int(preset["offset"])
    if args.max_frames is None:
        args.max_frames = int(preset["max_frames"])
    if args.cols is None:
        args.cols = int(preset["cols"])
    if args.rows is None:
        args.rows = int(preset["rows"])
    if args.selection_mode == "head":
        args.selection_mode = str(preset.get("selection_mode", "head"))
    if args.fps == 24.0:
        args.fps = float(preset["fps"])
    if args.ini_target is None:
        args.ini_target = str(preset["ini_target"])
    if not args.progress_driven and bool(preset["progress_driven"]):
        args.progress_driven = True
    if args.start_time_mode == 0 and int(preset["start_time_mode"]) != 0:
        args.start_time_mode = int(preset["start_time_mode"])

    return preset


def main() -> int:
    args = parse_args()
    preset = apply_preset_defaults(args)

    if preset is not None:
        print(f"Preset: {args.preset} -> {preset['label']}")

    frames = collect_frames(args.input)
    if not frames:
        print(f"No frames matched pattern: {args.input}", file=sys.stderr)
        return 1

    selected = select_frames(frames, args.offset, args.max_frames, args.selection_mode)
    if not selected:
        print("No frames left after applying offset/max-frames.", file=sys.stderr)
        return 1

    cols, rows = compute_grid(len(selected), args.cols, args.rows)

    print(f"Matched frames: {len(frames)}")
    print(
        f"Selected frames: {len(selected)} "
        f"(offset={args.offset}, max={args.max_frames}, mode={args.selection_mode})"
    )
    print(f"Grid: {cols} x {rows} (capacity={cols * rows})")
    print(f"Output atlas: {args.output}")

    if args.dry_run:
        return 0

    images = load_frames(selected)
    atlas = build_atlas(images, cols, rows)

    output_path = Path(args.output)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    atlas.save(output_path, "PNG")
    print(f"Saved atlas: {output_path} ({atlas.width}x{atlas.height})")

    if args.ini_target:
        path_in_ini = args.ini_path if args.ini_path else default_ini_path_for_output(output_path)
        snippet = make_ini_snippet(
            prefix=args.ini_target,
            path_in_ini=path_in_ini,
            cols=cols,
            rows=rows,
            frame_count=len(selected),
            fps=args.fps,
            loop=args.loop,
            progress_driven=args.progress_driven,
            start_time_mode=args.start_time_mode,
        )
        if args.ini_out:
            snippet_path = Path(args.ini_out)
        else:
            snippet_path = output_path.with_suffix(".snippet.ini")
        snippet_path.parent.mkdir(parents=True, exist_ok=True)
        snippet_path.write_text(snippet, encoding="utf-8")
        print(f"Saved snippet: {snippet_path}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
