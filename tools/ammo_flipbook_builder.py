#!/usr/bin/env python3
"""
AmmoWheel Flipbook Builder
==========================
Generates flipbook PNG sequences and static PNGs for Wheeler's AmmoWheel reskin system.

Update (v1.3) based on the provided asset-dimension findings
1) Arc slot assets are treated as "unwrapped arc rectangles" (width = arc length, height = ring thickness),
   not wheel-diameter wedges. This matches the reported rendered bounds per slot wedge.
2) Slot wedge math:
   inner = WheelRadius * InnerRadiusRatio
   thickness = WheelRadius - inner
   usableAngle = (360 / SlotCount) - SlotGapDeg
   midRadius = (WheelRadius + inner) / 2
   arcWidth = midRadius * radians(usableAngle)   (default, can switch to outerRadius mode)
3) Adds ExportScale (default 2.0) to generate 2x assets for headroom, as suggested in the report.
4) Adds SlotCount control (default 8).
5) Adds SlotShape support (Arc or Circle) from INI [Appearance] SlotShape.
6) Adds BorderRing target as a ring texture (uses BorderInnerScale / BorderOuterScale for inner cutout ratio).
7) Preserves original source alpha when applying circle/ring masks (alpha multiplied by mask).

Supports:
- Animated GIF sources (extracts frames)
- SVG sources (converts to PNG, requires cairosvg)
- Static PNG/JPG sources
- Folder of PNG frames

Version: 1.3
"""

import tkinter as tk
from tkinter import ttk, filedialog, messagebox, scrolledtext
from pathlib import Path
import math
import configparser
from dataclasses import dataclass
from enum import Enum
from typing import Dict, Tuple, List, Optional

from PIL import Image, ImageDraw, ImageSequence, ImageChops

try:
    import cairosvg  # type: ignore
    SVG_SUPPORT = True
except ImportError:
    SVG_SUPPORT = False
    print("Warning: cairosvg not installed. SVG support disabled.")
    print("Install with: pip install cairosvg")


class DefaultGeometry:
    WHEEL_RADIUS = 340.0
    INNER_RADIUS_RATIO = 0.55
    ICON_SIZE_PX = 64.0
    POPUP_BUBBLE_RADIUS = 85.0
    SLOT_GAP_DEG = 1.5
    DEFAULT_SLOT_COUNT = 8

    # Appearance
    SLOT_SHAPE = 0            # 0 Arc, 1 Circle
    SLOT_SHAPE_SCALE = 1.0

    # Visual polish
    BACKGROUND_RADIUS_SCALE = 1.0
    BORDER_INNER_SCALE = 1.0
    BORDER_OUTER_SCALE = 1.0
    SLOT_SHADOW_OFFSET_X = 0.0
    SLOT_SHADOW_OFFSET_Y = 0.0
    SLOT_HIGHLIGHT_THICKNESS = 0.0
    SELECTED_INDICATOR_THICKNESS = 3.0
    LOW_AMMO_INDICATOR_THICKNESS = 3.0

    @staticmethod
    def inner_radius(outer_radius: float, ratio: float) -> float:
        return outer_radius * ratio


class RotationMode(Enum):
    FOLLOW_SLOT = 0
    UPRIGHT = 1
    FIXED = 2


class ArcWidthMode(Enum):
    MID_RADIUS = "mid"
    OUTER_RADIUS = "outer"


@dataclass
class TargetDefinition:
    name: str
    ini_prefix: str
    subfolder: str
    rotation_mode: RotationMode
    shape_type: str          # "arc_unwrapped", "circle", "ring"
    size_calculation: str    # "slot", "icon", "popup", "wheel", "border"
    description: str
    default_rotation_offset: float = 0.0


VISUAL_TARGETS: Dict[str, TargetDefinition] = {
    "SlotBackground": TargetDefinition(
        name="Slot Background",
        ini_prefix="SlotBackground",
        subfolder="slot_bg",
        rotation_mode=RotationMode.FOLLOW_SLOT,
        shape_type="arc_unwrapped",
        size_calculation="slot",
        description="Arc slot wedge as unwrapped rectangle (arc length x thickness).",
    ),
    "SlotFrame": TargetDefinition(
        name="Slot Frame",
        ini_prefix="SlotFrame",
        subfolder="slot_frame",
        rotation_mode=RotationMode.FOLLOW_SLOT,
        shape_type="arc_unwrapped",
        size_calculation="slot",
        description="Arc slot frame as unwrapped rectangle (arc length x thickness).",
    ),
    "Popup": TargetDefinition(
        name="Popup Overlay",
        ini_prefix="Popup",
        subfolder="popup_overlay",
        rotation_mode=RotationMode.UPRIGHT,
        shape_type="arc_unwrapped",
        size_calculation="slot",
        description="Hover overlay uses slot wedge bounds (unwrapped rectangle).",
    ),
    "IndicatorSelected": TargetDefinition(
        name="Indicator Selected",
        ini_prefix="IndicatorSelected",
        subfolder="indicator_selected",
        rotation_mode=RotationMode.FOLLOW_SLOT,
        shape_type="arc_unwrapped",
        size_calculation="slot",
        description="Indicator drawn over slot arc, keep same slot unwrapped bounds.",
    ),
    "IndicatorHovered": TargetDefinition(
        name="Indicator Hovered",
        ini_prefix="IndicatorHovered",
        subfolder="indicator_hovered",
        rotation_mode=RotationMode.FOLLOW_SLOT,
        shape_type="arc_unwrapped",
        size_calculation="slot",
        description="Hover indicator drawn over slot arc, keep same slot unwrapped bounds.",
    ),
    "SlotIcon": TargetDefinition(
        name="Slot Icon",
        ini_prefix="SlotIcon",
        subfolder="slot_icon",
        rotation_mode=RotationMode.FOLLOW_SLOT,
        shape_type="circle",
        size_calculation="icon",
        description="Icon texture (IconSizePx).",
        default_rotation_offset=90.0,
    ),
    "PopupBubble": TargetDefinition(
        name="Popup Bubble",
        ini_prefix="PopupBubble",
        subfolder="popup",
        rotation_mode=RotationMode.UPRIGHT,
        shape_type="circle",
        size_calculation="popup",
        description="Popup bubble diameter = 2 * BubbleRadius.",
    ),
    "WheelBackground": TargetDefinition(
        name="Wheel Background",
        ini_prefix="WheelBackground",
        subfolder="wheel_bg",
        rotation_mode=RotationMode.UPRIGHT,
        shape_type="circle",
        size_calculation="wheel",
        description="Wheel background diameter = 2 * WheelRadius * BackgroundRadiusScale.",
    ),
    "BorderRing": TargetDefinition(
        name="Border Ring",
        ini_prefix="BorderRing",
        subfolder="border_ring",
        rotation_mode=RotationMode.UPRIGHT,
        shape_type="ring",
        size_calculation="border",
        description="Border ring diameter = 2 * WheelRadius * BorderOuterScale, inner cut by BorderInnerScale.",
    ),
}


class AmmoWheelINIParser:
    """
    Loads config from:
    1) <mod_root>/SKSE/Plugins/wheeler/AmmoWheel_Override.ini
    2) <script_dir>/AmmoWheel_Override.ini
    3) <mod_root>/SKSE/Plugins/wheeler/AmmoWheel.ini
    """

    def __init__(self, mod_root: str):
        self.mod_root = Path(mod_root)
        wheeler_dir = self.mod_root / "SKSE" / "Plugins" / "wheeler"

        candidates = [
            wheeler_dir / "AmmoWheel_Override.ini",
            Path(__file__).with_name("AmmoWheel_Override.ini"),
            wheeler_dir / "AmmoWheel.ini",
        ]
        self.ini_path = next((p for p in candidates if p.exists()), wheeler_dir / "AmmoWheel.ini")

    def parse(self) -> Dict[str, float]:
        if not self.ini_path.exists():
            return self._defaults()

        parser = configparser.ConfigParser()
        parser.read(self.ini_path, encoding="utf-8")

        cfg: Dict[str, float] = {}

        if parser.has_section("Position"):
            cfg["WheelRadius"] = parser.getfloat("Position", "WheelRadius", fallback=DefaultGeometry.WHEEL_RADIUS)

        if parser.has_section("Geometry"):
            cfg["InnerRadiusRatio"] = parser.getfloat("Geometry", "InnerRadiusRatio", fallback=DefaultGeometry.INNER_RADIUS_RATIO)
            cfg["SlotGapDeg"] = parser.getfloat("Geometry", "SlotGapDeg", fallback=DefaultGeometry.SLOT_GAP_DEG)
            cfg["IconSizePx"] = parser.getfloat("Geometry", "IconSizePx", fallback=DefaultGeometry.ICON_SIZE_PX)

        if parser.has_section("Popup"):
            cfg["PopupBubbleRadius"] = parser.getfloat("Popup", "BubbleRadius", fallback=DefaultGeometry.POPUP_BUBBLE_RADIUS)

        if parser.has_section("Appearance"):
            cfg["SlotShape"] = parser.getfloat("Appearance", "SlotShape", fallback=DefaultGeometry.SLOT_SHAPE)
            cfg["SlotShapeScale"] = parser.getfloat("Appearance", "SlotShapeScale", fallback=DefaultGeometry.SLOT_SHAPE_SCALE)

        if parser.has_section("VisualPolish"):
            cfg["BackgroundRadiusScale"] = parser.getfloat("VisualPolish", "BackgroundRadiusScale", fallback=DefaultGeometry.BACKGROUND_RADIUS_SCALE)
            cfg["BorderInnerScale"] = parser.getfloat("VisualPolish", "BorderInnerScale", fallback=DefaultGeometry.BORDER_INNER_SCALE)
            cfg["BorderOuterScale"] = parser.getfloat("VisualPolish", "BorderOuterScale", fallback=DefaultGeometry.BORDER_OUTER_SCALE)
            cfg["SlotShadowOffsetX"] = parser.getfloat("VisualPolish", "SlotShadowOffsetX", fallback=DefaultGeometry.SLOT_SHADOW_OFFSET_X)
            cfg["SlotShadowOffsetY"] = parser.getfloat("VisualPolish", "SlotShadowOffsetY", fallback=DefaultGeometry.SLOT_SHADOW_OFFSET_Y)
            cfg["SlotHighlightThickness"] = parser.getfloat("VisualPolish", "SlotHighlightThickness", fallback=DefaultGeometry.SLOT_HIGHLIGHT_THICKNESS)

        if parser.has_section("Indicators"):
            cfg["SelectedIndicatorThickness"] = parser.getfloat("Indicators", "SelectedIndicatorThickness", fallback=DefaultGeometry.SELECTED_INDICATOR_THICKNESS)
            cfg["LowAmmoIndicatorThickness"] = parser.getfloat("Indicators", "LowAmmoIndicatorThickness", fallback=DefaultGeometry.LOW_AMMO_INDICATOR_THICKNESS)

        defaults = self._defaults()
        for k, v in defaults.items():
            cfg.setdefault(k, v)

        return cfg

    def _defaults(self) -> Dict[str, float]:
        return {
            "WheelRadius": DefaultGeometry.WHEEL_RADIUS,
            "InnerRadiusRatio": DefaultGeometry.INNER_RADIUS_RATIO,
            "SlotGapDeg": DefaultGeometry.SLOT_GAP_DEG,
            "IconSizePx": DefaultGeometry.ICON_SIZE_PX,
            "PopupBubbleRadius": DefaultGeometry.POPUP_BUBBLE_RADIUS,
            "SlotShape": float(DefaultGeometry.SLOT_SHAPE),
            "SlotShapeScale": DefaultGeometry.SLOT_SHAPE_SCALE,
            "BackgroundRadiusScale": DefaultGeometry.BACKGROUND_RADIUS_SCALE,
            "BorderInnerScale": DefaultGeometry.BORDER_INNER_SCALE,
            "BorderOuterScale": DefaultGeometry.BORDER_OUTER_SCALE,
            "SlotShadowOffsetX": DefaultGeometry.SLOT_SHADOW_OFFSET_X,
            "SlotShadowOffsetY": DefaultGeometry.SLOT_SHADOW_OFFSET_Y,
            "SlotHighlightThickness": DefaultGeometry.SLOT_HIGHLIGHT_THICKNESS,
            "SelectedIndicatorThickness": DefaultGeometry.SELECTED_INDICATOR_THICKNESS,
            "LowAmmoIndicatorThickness": DefaultGeometry.LOW_AMMO_INDICATOR_THICKNESS,
        }


class GeometryCalculator:
    """
    Computes rendered bounds using the provided report math.
    Slot assets are unwrapped rectangles:
      width  = arcLength(radiusMode) * SlotShapeScale
      height = (outer-inner) * SlotShapeScale
    Then multiplied by ExportScale (default 2.0).
    """

    def __init__(self, cfg: Dict[str, float], slot_count: int, export_scale: float, arc_width_mode: ArcWidthMode):
        self.cfg = cfg
        self.slot_count = max(1, int(slot_count))
        self.export_scale = max(0.1, float(export_scale))
        self.arc_width_mode = arc_width_mode

        self.wheel_radius = float(cfg["WheelRadius"])
        self.inner_ratio = float(cfg["InnerRadiusRatio"])
        self.slot_gap_deg = float(cfg["SlotGapDeg"])
        self.icon_size_px = float(cfg["IconSizePx"])
        self.popup_radius = float(cfg["PopupBubbleRadius"])

        self.slot_shape = int(float(cfg.get("SlotShape", 0.0)))
        self.slot_shape_scale = float(cfg.get("SlotShapeScale", 1.0))

        self.background_radius_scale = float(cfg.get("BackgroundRadiusScale", 1.0))
        self.border_inner_scale = float(cfg.get("BorderInnerScale", 1.0))
        self.border_outer_scale = float(cfg.get("BorderOuterScale", 1.0))

        self.inner_radius = DefaultGeometry.inner_radius(self.wheel_radius, self.inner_ratio)
        self.thickness = max(1.0, self.wheel_radius - self.inner_radius)

        self.usable_angle_deg = (360.0 / float(self.slot_count)) - float(self.slot_gap_deg)
        self.usable_angle_deg = max(1.0, self.usable_angle_deg)
        self.usable_angle_rad = math.radians(self.usable_angle_deg)

        self.mid_radius = (self.wheel_radius + self.inner_radius) / 2.0
        self.arc_length_mid = self.mid_radius * self.usable_angle_rad
        self.arc_length_outer = self.wheel_radius * self.usable_angle_rad

    def slot_rect_rendered(self) -> Tuple[float, float]:
        if self.slot_shape == 1:
            # Circle slots. Use thickness as base diameter, scaled by SlotShapeScale.
            d = self.thickness * self.slot_shape_scale
            return (d, d)

        arc_w = self.arc_length_mid if self.arc_width_mode == ArcWidthMode.MID_RADIUS else self.arc_length_outer
        w = arc_w * self.slot_shape_scale
        h = self.thickness * self.slot_shape_scale
        return (w, h)

    def target_canvas_size(self, target: TargetDefinition) -> Tuple[int, int]:
        if target.size_calculation == "icon":
            s = int(math.ceil(self.icon_size_px * self.export_scale))
            return (s, s)

        if target.size_calculation == "popup":
            d = int(math.ceil((self.popup_radius * 2.0) * self.export_scale))
            return (d, d)

        if target.size_calculation == "wheel":
            d = int(math.ceil((self.wheel_radius * 2.0 * self.background_radius_scale) * self.export_scale))
            return (d, d)

        if target.size_calculation == "border":
            d = int(math.ceil((self.wheel_radius * 2.0 * self.border_outer_scale) * self.export_scale))
            return (d, d)

        # slot
        w, h = self.slot_rect_rendered()
        W = int(math.ceil(w * self.export_scale))
        H = int(math.ceil(h * self.export_scale))
        W = max(1, W)
        H = max(1, H)
        return (W, H)

    def border_ring_inner_ratio(self) -> float:
        # Ring mask innerRatio for circle mask generator.
        # Outer radius uses BorderOuterScale, inner radius uses BorderInnerScale.
        # Ratio = inner / outer.
        o = max(1e-6, self.border_outer_scale)
        i = max(0.0, self.border_inner_scale)
        return max(0.0, min(0.999, i / o))

    def report_lines(self) -> List[str]:
        slot_w, slot_h = self.slot_rect_rendered()
        lines = []
        lines.append("Computed geometry report")
        lines.append(f"  SlotCount: {self.slot_count}")
        lines.append(f"  WheelRadius: {self.wheel_radius:.2f}px")
        lines.append(f"  InnerRadiusRatio: {self.inner_ratio:.4f}")
        lines.append(f"  InnerRadius: {self.inner_radius:.2f}px")
        lines.append(f"  Thickness: {self.thickness:.2f}px")
        lines.append(f"  SlotGapDeg: {self.slot_gap_deg:.2f} deg")
        lines.append(f"  UsableAngle: {self.usable_angle_deg:.2f} deg")
        lines.append(f"  MidRadius: {self.mid_radius:.2f}px")
        lines.append(f"  ArcLength(mid): {self.arc_length_mid:.2f}px")
        lines.append(f"  ArcLength(outer): {self.arc_length_outer:.2f}px")
        lines.append(f"  SlotShape: {self.slot_shape} (0 arc, 1 circle)")
        lines.append(f"  SlotShapeScale: {self.slot_shape_scale:.3f}")
        lines.append(f"  ArcWidthMode: {self.arc_width_mode.value}")
        lines.append(f"  SlotRect(rendered): {slot_w:.2f} x {slot_h:.2f} px")
        lines.append(f"  ExportScale: {self.export_scale:.2f}")
        lines.append(f"  SlotRect(asset): {slot_w * self.export_scale:.2f} x {slot_h * self.export_scale:.2f} px")
        lines.append(f"  IconSizePx(rendered): {self.icon_size_px:.2f}px")
        lines.append(f"  IconSize(asset): {self.icon_size_px * self.export_scale:.2f}px")
        lines.append(f"  PopupBubbleRadius: {self.popup_radius:.2f}px, Diameter(asset): {self.popup_radius * 2.0 * self.export_scale:.2f}px")
        lines.append(f"  BackgroundRadiusScale: {self.background_radius_scale:.3f}")
        lines.append(f"  BorderOuterScale: {self.border_outer_scale:.3f}, BorderInnerScale: {self.border_inner_scale:.3f}")
        lines.append(f"  BorderRing innerRatio(mask): {self.border_ring_inner_ratio():.4f}")
        return lines


class MaskGenerator:
    @staticmethod
    def circle_or_ring_mask(size: Tuple[int, int], inner_ratio: float = 0.0, supersample: int = 4) -> Image.Image:
        w, h = size
        W, H = w * supersample, h * supersample

        mask_hi = Image.new("L", (W, H), 0)
        draw = ImageDraw.Draw(mask_hi)

        cx, cy = W // 2, H // 2
        outer_r = min(W, H) // 2
        inner_r = int(outer_r * max(0.0, min(0.999, inner_ratio)))

        bbox_outer = [cx - outer_r, cy - outer_r, cx + outer_r, cy + outer_r]
        draw.ellipse(bbox_outer, fill=255)

        if inner_r > 0:
            bbox_inner = [cx - inner_r, cy - inner_r, cx + inner_r, cy + inner_r]
            draw.ellipse(bbox_inner, fill=0)

        return mask_hi.resize((w, h), Image.Resampling.LANCZOS)


class ImageProcessor:
    class ResizeMode(Enum):
        FILL = "fill"
        FIT = "fit"
        STRETCH = "stretch"

    def __init__(self, geom: GeometryCalculator):
        self.geom = geom
        self.mask_gen = MaskGenerator()

    def extract_frames_from_source(self, source_path: str) -> List[Image.Image]:
        p = Path(source_path)
        ext = p.suffix.lower()

        if ext == ".gif":
            return self._extract_gif_frames(str(p))
        if ext == ".svg":
            return self._convert_svg_to_frames(str(p))
        if ext in [".png", ".jpg", ".jpeg", ".bmp"]:
            return [Image.open(p).convert("RGBA")]
        raise ValueError(f"Unsupported file format: {ext}")

    def _extract_gif_frames(self, gif_path: str) -> List[Image.Image]:
        frames: List[Image.Image] = []
        with Image.open(gif_path) as gif:
            animated = True
            try:
                gif.seek(1)
            except EOFError:
                animated = False
            gif.seek(0)

            if animated:
                for frame in ImageSequence.Iterator(gif):
                    frames.append(frame.convert("RGBA").copy())
            else:
                frames.append(gif.convert("RGBA").copy())
        return frames

    def _convert_svg_to_frames(self, svg_path: str) -> List[Image.Image]:
        if not SVG_SUPPORT:
            raise RuntimeError("SVG support not available. Install cairosvg: pip install cairosvg")

        with open(svg_path, "rb") as f:
            svg_data = f.read()

        png_bytes = cairosvg.svg2png(bytestring=svg_data, output_width=2048, output_height=2048)

        from io import BytesIO
        img = Image.open(BytesIO(png_bytes)).convert("RGBA")
        return [img]

    def process_frame(self, source_frame: Image.Image, target: TargetDefinition,
                      resize_mode: "ImageProcessor.ResizeMode", apply_mask: bool) -> Image.Image:
        canvas_size = self.geom.target_canvas_size(target)
        resized = self._resize_image(source_frame, canvas_size, resize_mode)

        if apply_mask:
            resized = self._apply_mask(resized, target, canvas_size)

        return resized

    def _resize_image(self, image: Image.Image, target_size: Tuple[int, int],
                      mode: "ImageProcessor.ResizeMode") -> Image.Image:
        img = image.copy()

        if mode == self.ResizeMode.STRETCH:
            return img.resize(target_size, Image.Resampling.LANCZOS)

        if mode == self.ResizeMode.FIT:
            img.thumbnail(target_size, Image.Resampling.LANCZOS)
            canvas = Image.new("RGBA", target_size, (0, 0, 0, 0))
            off = ((target_size[0] - img.width) // 2, (target_size[1] - img.height) // 2)
            canvas.paste(img, off)
            return canvas

        # FILL
        img_ratio = img.width / img.height
        tgt_ratio = target_size[0] / target_size[1]

        if img_ratio > tgt_ratio:
            new_h = target_size[1]
            new_w = int(new_h * img_ratio)
        else:
            new_w = target_size[0]
            new_h = int(new_w / img_ratio)

        resized = img.resize((new_w, new_h), Image.Resampling.LANCZOS)
        left = (new_w - target_size[0]) // 2
        top = (new_h - target_size[1]) // 2
        return resized.crop((left, top, left + target_size[0], top + target_size[1]))

    def _apply_mask(self, image: Image.Image, target: TargetDefinition,
                    canvas_size: Tuple[int, int]) -> Image.Image:
        if image.mode != "RGBA":
            image = image.convert("RGBA")

        # Unwrapped arc assets should remain rectangular. No wedge mask here.
        if target.shape_type == "arc_unwrapped":
            return image

        if target.shape_type == "circle":
            mask = self.mask_gen.circle_or_ring_mask(canvas_size, inner_ratio=0.0, supersample=4)

        elif target.shape_type == "ring":
            inner_ratio = self.geom.border_ring_inner_ratio()
            mask = self.mask_gen.circle_or_ring_mask(canvas_size, inner_ratio=inner_ratio, supersample=4)

        else:
            return image

        old_alpha = image.getchannel("A")
        new_alpha = ImageChops.multiply(old_alpha, mask)
        out = image.copy()
        out.putalpha(new_alpha)
        return out


class INISnippetGenerator:
    @staticmethod
    def generate_snippet(target: TargetDefinition, frame_count: int, fps: float,
                         loop: bool, is_flipbook: bool, output_name: str) -> str:
        rotation_mode_map = {
            RotationMode.FOLLOW_SLOT: "0",
            RotationMode.UPRIGHT: "1",
            RotationMode.FIXED: "2",
        }
        rotation_mode_str = rotation_mode_map[target.rotation_mode]

        if is_flipbook:
            path = f"{target.subfolder}/{output_name}_{{:02}}.png"
        else:
            path = f"{target.subfolder}/{output_name}.png"

        s = []
        s.append(f"; {target.name}")
        s.append(f"{target.ini_prefix}_Enabled = true")
        s.append(f"{target.ini_prefix}_IsFlipbook = {'true' if is_flipbook else 'false'}")
        s.append(f"{target.ini_prefix}_Path = {path}")

        if is_flipbook:
            s.append(f"{target.ini_prefix}_FrameCount = {frame_count}")
            s.append(f"{target.ini_prefix}_FPS = {fps}")
            s.append(f"{target.ini_prefix}_Loop = {'true' if loop else 'false'}")

        s.append(f"{target.ini_prefix}_RotationMode = {rotation_mode_str}")
        s.append(f"{target.ini_prefix}_RotationOffsetDeg = {target.default_rotation_offset}")
        s.append(f"{target.ini_prefix}_Alpha = 1.0")
        return "\n".join(s) + "\n"


class FlipbookBuilderGUI:
    def __init__(self, root: tk.Tk):
        self.root = root
        self.root.title("AmmoWheel Flipbook Builder v1.3")
        self.root.geometry("980x900")

        self.mod_root = tk.StringVar()
        self.source_folder = tk.StringVar()
        self.output_name = tk.StringVar(value="my_asset")
        self.target_type = tk.StringVar(value="SlotBackground")

        self.is_flipbook = tk.BooleanVar(value=True)
        self.frame_count = tk.IntVar(value=16)
        self.fps = tk.DoubleVar(value=24.0)
        self.loop = tk.BooleanVar(value=True)

        self.resize_mode = tk.StringVar(value="fill")
        self.apply_mask = tk.BooleanVar(value=True)

        self.slot_count = tk.IntVar(value=DefaultGeometry.DEFAULT_SLOT_COUNT)
        self.export_scale = tk.DoubleVar(value=2.0)
        self.arc_width_mode = tk.StringVar(value=ArcWidthMode.MID_RADIUS.value)

        self.loaded_ini_path: Optional[Path] = None
        self.cfg: Dict[str, float] = {}

        self._build_ui()

    def _build_ui(self) -> None:
        main = ttk.Frame(self.root, padding="10")
        main.grid(row=0, column=0, sticky=(tk.W, tk.E, tk.N, tk.S))
        row = 0

        ttk.Label(main, text="Mod Root Folder:", font=("Arial", 10, "bold")).grid(row=row, column=0, sticky=tk.W, pady=5)
        row += 1
        ttk.Entry(main, textvariable=self.mod_root, width=70).grid(row=row, column=0, columnspan=2, sticky=(tk.W, tk.E))
        ttk.Button(main, text="Browse...", command=self._browse_mod_root).grid(row=row, column=2, padx=5)
        row += 1

        ttk.Label(main, text="Source (file or folder):", font=("Arial", 10, "bold")).grid(row=row, column=0, sticky=tk.W, pady=5)
        row += 1
        ttk.Entry(main, textvariable=self.source_folder, width=70).grid(row=row, column=0, columnspan=2, sticky=(tk.W, tk.E))
        ttk.Button(main, text="Browse...", command=self._browse_source).grid(row=row, column=2, padx=5)
        row += 1

        ttk.Label(main, text="Targets:", font=("Arial", 10, "bold")).grid(row=row, column=0, sticky=tk.W, pady=5)
        row += 1

        target_frame = ttk.Frame(main)
        target_frame.grid(row=row, column=0, columnspan=3, sticky=(tk.W, tk.E))
        for idx, (k, td) in enumerate(VISUAL_TARGETS.items()):
            ttk.Radiobutton(
                target_frame,
                text=f"{td.name} ({td.shape_type})",
                variable=self.target_type,
                value=k,
            ).grid(row=idx // 2, column=idx % 2, sticky=tk.W, padx=10, pady=2)
        row += 1

        ttk.Separator(main, orient="horizontal").grid(row=row, column=0, columnspan=3, sticky=(tk.W, tk.E), pady=10)
        row += 1

        geom_frame = ttk.LabelFrame(main, text="Geometry Controls", padding="10")
        geom_frame.grid(row=row, column=0, columnspan=3, sticky=(tk.W, tk.E))

        ttk.Label(geom_frame, text="SlotCount:").grid(row=0, column=0, sticky=tk.W)
        ttk.Spinbox(geom_frame, from_=1, to=32, textvariable=self.slot_count, width=8).grid(row=0, column=1, padx=5, sticky=tk.W)

        ttk.Label(geom_frame, text="ExportScale:").grid(row=0, column=2, sticky=tk.W, padx=(20, 0))
        ttk.Spinbox(geom_frame, from_=0.5, to=4.0, increment=0.25, textvariable=self.export_scale, width=8).grid(row=0, column=3, padx=5, sticky=tk.W)

        ttk.Label(geom_frame, text="ArcWidthMode:").grid(row=0, column=4, sticky=tk.W, padx=(20, 0))
        ttk.Combobox(
            geom_frame,
            textvariable=self.arc_width_mode,
            values=[ArcWidthMode.MID_RADIUS.value, ArcWidthMode.OUTER_RADIUS.value],
            state="readonly",
            width=10,
        ).grid(row=0, column=5, padx=5, sticky=tk.W)

        row += 1

        ttk.Separator(main, orient="horizontal").grid(row=row, column=0, columnspan=3, sticky=(tk.W, tk.E), pady=10)
        row += 1

        ttk.Checkbutton(main, text="Generate Flipbook (sequence)", variable=self.is_flipbook).grid(row=row, column=0, sticky=tk.W)
        row += 1

        fb = ttk.LabelFrame(main, text="Flipbook Settings", padding="10")
        fb.grid(row=row, column=0, columnspan=3, sticky=(tk.W, tk.E))

        ttk.Label(fb, text="Frame Count:").grid(row=0, column=0, sticky=tk.W)
        ttk.Spinbox(fb, from_=1, to=240, textvariable=self.frame_count, width=8).grid(row=0, column=1, padx=5, sticky=tk.W)

        ttk.Label(fb, text="FPS:").grid(row=0, column=2, sticky=tk.W, padx=(20, 0))
        ttk.Spinbox(fb, from_=1.0, to=60.0, textvariable=self.fps, width=8).grid(row=0, column=3, padx=5, sticky=tk.W)

        ttk.Checkbutton(fb, text="Loop", variable=self.loop).grid(row=0, column=4, padx=20, sticky=tk.W)
        row += 1

        opt = ttk.LabelFrame(main, text="Processing Options", padding="10")
        opt.grid(row=row, column=0, columnspan=3, sticky=(tk.W, tk.E))

        ttk.Label(opt, text="Resize Mode:").grid(row=0, column=0, sticky=tk.W)
        ttk.Radiobutton(opt, text="Fill", variable=self.resize_mode, value="fill").grid(row=0, column=1, padx=10, sticky=tk.W)
        ttk.Radiobutton(opt, text="Fit", variable=self.resize_mode, value="fit").grid(row=0, column=2, padx=10, sticky=tk.W)
        ttk.Radiobutton(opt, text="Stretch", variable=self.resize_mode, value="stretch").grid(row=0, column=3, padx=10, sticky=tk.W)

        ttk.Checkbutton(opt, text="Apply Mask (circle/ring only)", variable=self.apply_mask).grid(row=1, column=0, columnspan=3, sticky=tk.W, pady=5)
        row += 1

        ttk.Label(main, text="Output Name:", font=("Arial", 10, "bold")).grid(row=row, column=0, sticky=tk.W, pady=5)
        row += 1
        ttk.Entry(main, textvariable=self.output_name, width=40).grid(row=row, column=0, columnspan=2, sticky=(tk.W, tk.E))
        row += 1

        ttk.Button(main, text="Generate", command=self._generate).grid(row=row, column=0, columnspan=3, pady=14)
        row += 1

        ttk.Label(main, text="Log:", font=("Arial", 10, "bold")).grid(row=row, column=0, sticky=tk.W, pady=5)
        row += 1
        self.log_text = scrolledtext.ScrolledText(main, height=18, width=105)
        self.log_text.grid(row=row, column=0, columnspan=3, sticky=(tk.W, tk.E, tk.N, tk.S), pady=5)

        self.root.columnconfigure(0, weight=1)
        self.root.rowconfigure(0, weight=1)
        main.columnconfigure(0, weight=1)
        main.rowconfigure(row, weight=1)

    def _browse_mod_root(self) -> None:
        folder = filedialog.askdirectory(title="Select Mod Root Folder")
        if folder:
            self.mod_root.set(folder)
            self._log(f"Mod root set to: {folder}")
            self._load_ini()

    def _browse_source(self) -> None:
        file_types = [
            ("All Supported", "*.gif *.svg *.png *.jpg *.jpeg *.bmp"),
            ("Animated GIF", "*.gif"),
            ("SVG Vector", "*.svg"),
            ("Images", "*.png *.jpg *.jpeg *.bmp"),
            ("All Files", "*.*"),
        ]

        choice = messagebox.askquestion(
            "Source Selection",
            "Select a single file? Click 'No' to select a folder of PNGs instead.",
            icon="question",
        )

        if choice == "yes":
            file_path = filedialog.askopenfilename(title="Select Source File", filetypes=file_types)
            if file_path:
                self.source_folder.set(file_path)
                self._log(f"Source file set to: {file_path}")

                if file_path.lower().endswith(".gif"):
                    try:
                        with Image.open(file_path) as gif:
                            count = 0
                            try:
                                while True:
                                    gif.seek(count)
                                    count += 1
                            except EOFError:
                                pass
                        if count > 1:
                            self.frame_count.set(count)
                            self.is_flipbook.set(True)
                            self._log(f"Detected {count} frames in animated GIF")
                    except Exception as e:
                        self._log(f"Warning: Could not detect GIF frame count: {e}")
        else:
            folder = filedialog.askdirectory(title="Select Source Images Folder")
            if folder:
                self.source_folder.set(folder)
                self._log(f"Source folder set to: {folder}")

    def _load_ini(self) -> None:
        parser = AmmoWheelINIParser(self.mod_root.get())
        self.cfg = parser.parse()
        self.loaded_ini_path = parser.ini_path

        self._log("INI loaded")
        self._log(f"  INI path: {self.loaded_ini_path}")
        self._log(f"  WheelRadius: {self.cfg.get('WheelRadius')}")
        self._log(f"  InnerRadiusRatio: {self.cfg.get('InnerRadiusRatio')}")
        self._log(f"  SlotGapDeg: {self.cfg.get('SlotGapDeg')}")
        self._log(f"  IconSizePx: {self.cfg.get('IconSizePx')}")
        self._log(f"  PopupBubbleRadius: {self.cfg.get('PopupBubbleRadius')}")
        self._log(f"  SlotShape: {self.cfg.get('SlotShape')}")
        self._log(f"  SlotShapeScale: {self.cfg.get('SlotShapeScale')}")
        self._log(f"  BackgroundRadiusScale: {self.cfg.get('BackgroundRadiusScale')}")
        self._log(f"  BorderInnerScale: {self.cfg.get('BorderInnerScale')}")
        self._log(f"  BorderOuterScale: {self.cfg.get('BorderOuterScale')}")

    def _generate(self) -> None:
        if not self.mod_root.get():
            messagebox.showerror("Error", "Please select mod root folder")
            return
        if not self.source_folder.get():
            messagebox.showerror("Error", "Please select source file or folder")
            return
        if not self.cfg:
            self._load_ini()

        arc_mode = ArcWidthMode(self.arc_width_mode.get())
        geom = GeometryCalculator(
            cfg=self.cfg,
            slot_count=self.slot_count.get(),
            export_scale=self.export_scale.get(),
            arc_width_mode=arc_mode,
        )
        proc = ImageProcessor(geom)

        td = VISUAL_TARGETS[self.target_type.get()]
        canvas_size = geom.target_canvas_size(td)

        self._log("")
        self._log("=" * 70)
        self._log("Starting generation")
        self._log(f"Target: {td.name}")
        self._log(f"Canvas size: {canvas_size[0]} x {canvas_size[1]} px")

        for line in geom.report_lines():
            self._log(line)

        source_path = Path(self.source_folder.get())
        frames: List[Image.Image] = []

        try:
            if source_path.is_file():
                ext = source_path.suffix.lower()
                if ext == ".svg" and not SVG_SUPPORT:
                    messagebox.showerror("Error", "SVG support not available. Install cairosvg: pip install cairosvg")
                    return
                frames = proc.extract_frames_from_source(str(source_path))
                self._log(f"Loaded {len(frames)} frame(s) from file: {source_path.name}")

            elif source_path.is_dir():
                png_files = sorted(source_path.glob("*.png"))
                if not png_files:
                    messagebox.showerror("Error", "No PNG files found in source folder")
                    return
                for f in png_files:
                    frames.append(Image.open(f).convert("RGBA"))
                self._log(f"Loaded {len(frames)} frame(s) from folder")

            else:
                messagebox.showerror("Error", "Source path is neither file nor folder")
                return

            if not frames:
                messagebox.showerror("Error", "No frames extracted")
                return

            out_base = Path(self.mod_root.get()) / "SKSE" / "Plugins" / "wheeler" / "resources" / "ammo_wheel"
            out_dir = out_base / td.subfolder
            out_dir.mkdir(parents=True, exist_ok=True)

            resize_mode = ImageProcessor.ResizeMode(self.resize_mode.get())

            if self.is_flipbook.get():
                fc = min(self.frame_count.get(), len(frames))
                self._log(f"Generating flipbook: {fc} frames")

                for i in range(fc):
                    src = frames[i % len(frames)]
                    out_file = out_dir / f"{self.output_name.get()}_{i:02d}.png"
                    img = proc.process_frame(src, td, resize_mode, self.apply_mask.get())
                    img.save(out_file, "PNG")

                    if i % 5 == 0 or i == fc - 1:
                        self._log(f"Saved frame {i + 1}/{fc}")

                snippet = INISnippetGenerator.generate_snippet(
                    td, fc, self.fps.get(), self.loop.get(), True, self.output_name.get()
                )

            else:
                out_file = out_dir / f"{self.output_name.get()}.png"
                img = proc.process_frame(frames[0], td, resize_mode, self.apply_mask.get())
                img.save(out_file, "PNG")
                self._log(f"Saved static: {out_file.name}")

                snippet = INISnippetGenerator.generate_snippet(
                    td, 1, self.fps.get(), False, False, self.output_name.get()
                )

            snippet_file = out_dir / f"{self.output_name.get()}_snippet.ini"
            with open(snippet_file, "w", encoding="utf-8") as f:
                f.write(snippet)

            self._log("")
            self._log("INI Snippet")
            self._log(snippet.rstrip())
            self._log(f"Snippet saved: {snippet_file}")
            self._log("Done")

            messagebox.showinfo("Success", "Generation complete")

        except Exception as e:
            import traceback
            self._log(f"ERROR: {e}")
            self._log(traceback.format_exc())
            messagebox.showerror("Error", str(e))

    def _log(self, msg: str) -> None:
        self.log_text.insert(tk.END, msg + "\n")
        self.log_text.see(tk.END)
        self.root.update_idletasks()


def main() -> None:
    root = tk.Tk()
    app = FlipbookBuilderGUI(root)
    root.mainloop()


if __name__ == "__main__":
    main()
