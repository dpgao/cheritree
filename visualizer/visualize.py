#!/usr/bin/env python3
"""Visualize CHERI capability address space reachability.

Reads two JSONL files (capabilities and mappings) produced by cheritree and
visualizes the mapped address space as a horizontal rectangle, with
capability-reachable regions colored by permission type.  Regions reachable only
via sealed capabilities are overlaid with a hatch pattern.  Unmapped gaps are
omitted; mapped regions are stitched together using log-compressed widths so
that both small and large regions are visible.

By default the figure is shown in an interactive window.  Use -o/--output to
write a file instead: .pgf for PGF (includable in LaTeX with
\\input{output.pgf}), or .pdf for PDF.
"""

import argparse
import json
import math
from bisect import bisect_right
from dataclasses import dataclass

import matplotlib.pyplot as plt
from matplotlib.patches import Patch, Rectangle

COLORS = {
    (False, False): "tab:green",   # R
    (False, True):  "tab:red",     # RW
    (True,  False): "tab:blue",    # RX
    (True,  True):  "tab:brown",   # RWX
}

LABELS = {
    (False, False): "R",
    (False, True):  "RW",
    (True,  False): "RX",
    (True,  True):  "RWX",
}


def load_jsonl(path):
    # One JSON object per line
    entries = []
    with open(path) as f:
        for line in f:
            entries.append(json.loads(line))
    return entries


def parse_args():
    parser = argparse.ArgumentParser(
        description="Visualize CHERI capability address space reachability.")
    parser.add_argument("capabilities", help="JSONL file of capabilities")
    parser.add_argument("mappings", help="JSONL file of mappings")
    parser.add_argument("-o", "--output",
                        help="Output file (.pgf or .pdf). "
                             "If omitted, show an interactive window.")
    return parser.parse_args()


@dataclass
class Capability:
    """A single CHERI capability from the input JSONL."""
    base: int
    top: int
    perm_execute: bool
    perm_store: bool
    sealed: bool
    via_trampoline: bool


@dataclass
class CoordSystem:
    """Compressed display coordinate system for the mapped address space."""
    map_starts: list
    map_ends: list
    display_offsets: list  # length = len(mappings) + 1; last entry = total_width
    display_widths: list   # length = len(mappings) + 1; last entry = 0.0
    total_width: float

    def compress(self, addr):
        """Map a real address to a compressed display x-coordinate."""
        idx = bisect_right(self.map_starts, addr) - 1
        if idx < 0:
            raise ValueError(
                f"address {addr:#x} is before the first mapping "
                f"(starts at {self.map_starts[0]:#x})")
        start = self.map_starts[idx]
        end = self.map_ends[idx]
        if addr > end:
            raise ValueError(
                f"address {addr:#x} falls in a gap after mapping "
                f"[{start:#x}, {end:#x})")
        frac = (addr - start) / (end - start)
        return self.display_offsets[idx] + frac * self.display_widths[idx]


def parse_caps(caps_raw):
    """Parse raw capability records into Capability objects."""
    return [Capability(
                base=c["base"], top=c["top"],
                perm_execute=c["perm_execute"], perm_store=c["perm_store"],
                sealed=c["sealed"], via_trampoline=c.get("via_trampoline", False))
            for c in caps_raw]


def parse_mappings(maps_raw):
    """Sort, validate, and parse raw mapping records into (start, end, name, base) tuples."""
    maps_raw = sorted(maps_raw, key=lambda m: m["start"])
    for m in maps_raw:
        if m["end"] <= m["start"]:
            raise ValueError(
                f"empty mapping [{m['start']:#x}, {m['end']:#x}) "
                f"for {m['mapping']!r}")
    return [(m["start"], m["end"], m["mapping"], m["base"]) for m in maps_raw]


def build_coord_system(mappings):
    """Build a compressed coordinate system for the given mappings.

    Each mapping's display width is log2(region size), clamped to at least 1.0,
    so that both tiny and huge regions are visible.
    """
    map_starts = []
    map_ends = []
    display_offsets = []
    display_widths = []
    offset = 0.0

    for start, end, _name, _base in mappings:
        map_starts.append(start)
        map_ends.append(end)
        display_offsets.append(offset)
        w = max(math.log2(end - start), 1.0)
        display_widths.append(w)
        offset += w

    # Sentinel entry so display_offsets[len(mappings)] == total_width
    display_offsets.append(offset)
    display_widths.append(0.0)

    return CoordSystem(map_starts, map_ends, display_offsets, display_widths, offset)


def compute_colored_intervals(mappings, caps):
    """Compute (start, end, color, all_sealed) intervals for all mapped regions.

    For each mapping, capabilities are clipped to the mapping boundary and all
    boundary points are swept.  Each resulting segment is assigned:
    - a color based on the union of permissions (or tab:cyan if all covering
      capabilities have via_trampoline set), and
    - an all_sealed flag indicating whether every covering capability is sealed.
    """
    colored_intervals = []

    for m_start, m_end, _m_name, _m_base in mappings:
        points = {m_start, m_end}
        overlapping = []

        for cap in caps:
            if cap.base < m_end and cap.top > m_start:
                clipped_start = max(cap.base, m_start)
                clipped_end = min(cap.top, m_end)
                overlapping.append(Capability(
                    base=clipped_start, top=clipped_end,
                    perm_execute=cap.perm_execute, perm_store=cap.perm_store,
                    sealed=cap.sealed, via_trampoline=cap.via_trampoline))
                points.add(clipped_start)
                points.add(clipped_end)

        if not overlapping:
            continue

        sorted_points = sorted(points)
        for i in range(len(sorted_points) - 1):
            seg_start = sorted_points[i]
            seg_end = sorted_points[i + 1]

            has_execute = False
            has_store = False
            covered = False
            all_sealed = True
            all_trampoline = True

            for cap in overlapping:
                if cap.base <= seg_start and cap.top >= seg_end:
                    covered = True
                    has_execute = has_execute or cap.perm_execute
                    has_store = has_store or cap.perm_store
                    if not cap.sealed:
                        all_sealed = False
                    if not cap.via_trampoline:
                        all_trampoline = False

            if covered:
                if all_trampoline:
                    color = "tab:cyan"
                else:
                    color = COLORS[(has_execute, has_store)]
                colored_intervals.append((seg_start, seg_end, color, all_sealed))

    return colored_intervals


def render_figure(colored_intervals, coord_sys, mappings):
    """Render the address-space figure and return the matplotlib Figure."""
    rect_h = 0.5
    y_lo = -1.0                          # room for bracket labels below
    y_hi = rect_h + 0.5                  # room for 2-row legend above
    fig_width = 3.25
    fig_height = 0.5 / rect_h * (y_hi - y_lo)

    plt.rcParams.update({
        "font.family": "serif",
        "font.serif": ["Linux Libertine", "Times"],
        "font.size": 9,
        "hatch.linewidth": 0.25,
    })

    fig, ax = plt.subplots(figsize=(fig_width, fig_height))

    # Colored capability-reachable regions (with hatch for sealed-only)
    for seg_start, seg_end, color, sealed_only in colored_intervals:
        x0 = coord_sys.compress(seg_start)
        x1 = coord_sys.compress(seg_end)
        ax.add_patch(Rectangle(
            (x0, 0), x1 - x0, rect_h,
            facecolor=color, edgecolor="none"))
        if sealed_only:
            ax.add_patch(Rectangle(
                (x0, 0), x1 - x0, rect_h,
                facecolor="none", hatch="///"))

    # Top and bottom edges of the address-space rectangle
    ax.plot([0, coord_sys.total_width], [0, 0],
            color="black", linewidth=0.25, clip_on=False)
    ax.plot([0, coord_sys.total_width], [rect_h, rect_h],
            color="black", linewidth=0.25, clip_on=False)

    # Vertical lines at mapping boundaries
    for x in coord_sys.display_offsets:
        ax.plot([x, x], [0, rect_h],
                color="black", linewidth=0.25, clip_on=False)

    # Mapping labels with bracket lines (one per base group)
    bracket_y = -0.03
    tick_h = 0.03
    label_y = bracket_y - tick_h - 0.02

    base_groups = {}
    for mi, (m_start, m_end, m_name, m_base) in enumerate(mappings):
        if m_base not in base_groups:
            base_groups[m_base] = {
                "name": "",
                "x_min": coord_sys.display_offsets[mi],
                "x_max": coord_sys.display_offsets[mi] + coord_sys.display_widths[mi],
            }
        else:
            base_groups[m_base]["x_max"] = (
                coord_sys.display_offsets[mi] + coord_sys.display_widths[mi])

        if m_name and not base_groups[m_base]["name"]:
            name = m_name.rsplit("/", 1)[-1] if "/" in m_name else m_name
            if name.startswith("[") and name.endswith("]"):
                name = name[1:-1]
            base_groups[m_base]["name"] = name

    for info in base_groups.values():
        name = info["name"]
        if not name:
            continue
        x_min = info["x_min"]
        x_max = info["x_max"]
        x_center = (x_min + x_max) / 2

        # Draw bracket: left tick, horizontal bar, right tick
        ax.plot([x_min, x_min], [bracket_y, bracket_y - tick_h],
                color="black", linewidth=0.25)
        ax.plot([x_min, x_max], [bracket_y - tick_h, bracket_y - tick_h],
                color="black", linewidth=0.25)
        ax.plot([x_max, x_max], [bracket_y, bracket_y - tick_h],
                color="black", linewidth=0.25)

        ax.text(x_center, label_y, name,
                ha="right", va="center", rotation=90,
                rotation_mode="anchor")

    # Legend
    legend_handles = [
        Patch(facecolor=COLORS[k], label=LABELS[k])
        for k in [(False, False), (False, True), (True, False), (True, True)]
    ]
    legend_handles.append(Patch(facecolor="tab:cyan",
                                label="Only Via Trampoline"))
    legend_handles.append(Patch(facecolor="white", edgecolor="black",
                                hatch="////", label="Sealed"))
    rect_top_frac = (rect_h - y_lo) / (y_hi - y_lo)
    ax.legend(handles=legend_handles, loc="lower center",
              bbox_to_anchor=(0.5, rect_top_frac), ncol=3, columnspacing=0.75,
              borderpad=0.25, labelspacing=0.25, handletextpad=0.5)

    ax.set_xlim(0, coord_sys.total_width)
    ax.set_ylim(y_lo, y_hi)
    ax.set_aspect("auto")
    ax.axis("off")

    return fig


def main():
    args = parse_args()
    out_file = args.output

    caps = parse_caps(load_jsonl(args.capabilities))
    mappings = parse_mappings(load_jsonl(args.mappings))
    coord_sys = build_coord_system(mappings)
    colored_intervals = compute_colored_intervals(mappings, caps)
    fig = render_figure(colored_intervals, coord_sys, mappings)

    if out_file:
        fig.savefig(out_file, bbox_inches="tight", transparent=True)
        plt.close(fig)
    else:
        plt.show()


if __name__ == "__main__":
    main()
