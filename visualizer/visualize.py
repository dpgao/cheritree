#!/usr/bin/env python3
#
#  SPDX-License-Identifier: BSD-3-Clause
#
#  Copyright (c) 2026, Dapeng Gao.
#

import argparse
import json
import math
import re
from bisect import bisect_right
from collections import defaultdict
from dataclasses import dataclass, field

import matplotlib.pyplot as plt
from matplotlib.patches import Patch, Rectangle


def load_trace(path):
    """Load a CheriTree JSON trace object with mappings and capabilities."""
    with open(path) as f:
        trace = json.load(f)

    maps = [Mapping.from_dict(m) for m in trace["mappings"]]
    maps = [m for m in maps if m.prot_read or m.prot_write or m.prot_exec]
    maps.sort(key=lambda m: m.start)

    mappings = defaultdict(list)
    for m in maps:
        mappings[m.base].append(m)

    caps = [Capability.from_dict(c) for c in trace["capabilities"]]

    return mappings, caps


def parse_args():
    parser = argparse.ArgumentParser(
        description="Visualize CHERI capability address space reachability.")
    parser.add_argument("trace",
                        help="CheriTree JSON trace file containing mappings "
                             "and capabilities.")
    parser.add_argument("-o", "--output",
                        help="Output file. "
                             "If omitted, show an interactive window.")
    parser.add_argument("-W", "--width", type=float, default=3.25,
                        metavar="INCHES",
                        help="Figure width in inches (default: 3.25).")
    parser.add_argument("-H", "--height", type=float, default=0.5,
                        metavar="INCHES",
                        help="Height of the address-space rectangle in inches "
                             "(default: 0.5).")
    parser.add_argument("-f", "--font-size", type=float, default=9,
                        metavar="PT",
                        help="Font size in points (default: 9).")
    parser.add_argument("--why-brown", action="store_true",
                        help="For each RWX (brown) interval, print the "
                             "covering capabilities.")
    parser.add_argument("--mask-prot", action="store_true",
                        help="AND capability permissions with the underlying "
                             "mapping's protection bits.")
    parser.add_argument("--all-mappings", action="store_true",
                        help="Show all mappings, including unreachable ones.")
    return parser.parse_args()


@dataclass
class Capability:
    """A single CHERI capability from the input trace."""
    base: int
    top: int
    perm_store: bool
    perm_execute: bool
    sealed: bool
    via_trampoline: bool

    @classmethod
    def from_dict(cls, d):
        return cls(base=d["base"], top=d["top"],
                   perm_store=d["perm_store"],
                   perm_execute=d["perm_execute"],
                   sealed=d["sealed"],
                   via_trampoline=d.get("via_trampoline", False))


@dataclass
class Mapping:
    """A single memory mapping from the input trace."""
    start: int
    end: int
    name: str
    base: int
    prot_read: bool
    prot_write: bool
    prot_exec: bool
    overlapping_caps: list = field(default_factory=list)

    @classmethod
    def from_dict(cls, m):
        return cls(start=m["start"], end=m["end"],
                   name=m["mapping"], base=m["base"],
                   prot_read=m["prot_read"],
                   prot_write=m["prot_write"],
                   prot_exec=m["prot_exec"])


class CoordSystem:
    """
    Transform addresses to x-coordinates.

    Each mapping's display width is log2(region size), clamped to at least 1.0,
    so that both tiny and huge regions are visible.
    """
    def __init__(self, mappings):
        self.map_starts = []
        self.map_ends = []
        self.display_offsets = []
        self.display_widths = []
        offset = 0.0

        for ms in mappings.values():
            for m in ms:
                self.map_starts.append(m.start)
                self.map_ends.append(m.end)
                self.display_offsets.append(offset)
                w = max(math.log2(m.end - m.start), 1.0)
                self.display_widths.append(w)
                offset += w

        self.display_offsets.append(offset)
        self.display_widths.append(0.0)
        self.total_width = offset

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

    def draw_edges(self, ax, rect_h):
        """
        Draw the top and bottom horizontal edges of the address-space rectangle.
        """
        ax.plot([0, self.total_width], [0, 0],
                color="black", linewidth=1.0, clip_on=False)
        ax.plot([0, self.total_width], [rect_h, rect_h],
                color="black", linewidth=1.0, clip_on=False)


@dataclass
class Interval:
    """A colored, capability-covered sub-segment within a mapping."""
    _COLORS = {
        (False, False): "tab:green",   # R
        (False, True):  "tab:blue",    # RX
        (True,  False): "tab:red",     # RW
        (True,  True):  "tab:brown",   # RWX
    }

    start: int
    end: int
    covering: list
    has_store: bool
    has_execute: bool
    all_sealed: bool
    all_trampoline: bool

    @property
    def color(self):
        if self.all_trampoline:
            return "tab:cyan"
        return Interval._COLORS[(self.has_store, self.has_execute)]

    def union(self, cap):
        """
        Unify this interval's permissions with those of cap and add cap to
        covering.
        """
        if cap.base <= self.start and self.end <= cap.top:
            self.covering.append(cap)
            self.has_store      = self.has_store      or  cap.perm_store
            self.has_execute    = self.has_execute    or  cap.perm_execute
            self.all_sealed     = self.all_sealed     and cap.sealed
            self.all_trampoline = self.all_trampoline and cap.via_trampoline

    def draw(self, ax, coord_sys, rect_h):
        """
        Draw the colored rectangle and, if all covering caps are sealed, a hatch
        overlay.
        """
        x0 = coord_sys.compress(self.start)
        x1 = coord_sys.compress(self.end)
        ax.add_patch(Rectangle((x0, 0), x1 - x0, rect_h,
            facecolor=self.color, edgecolor="none", alpha=0.75))
        if self.all_sealed:
            ax.add_patch(Rectangle((x0, 0), x1 - x0, rect_h,
                facecolor="none", hatch="////"))


class BaseGroup:
    """
    Display metadata for a group of mappings that share the same base path.
    """
    def __init__(self, ms):
        self.mappings = ms
        for m in ms:
            name = m.name
            if not name:
                continue
            name = name.rsplit("/", 1)[-1]
            self._name = re.sub(r'\.so(\.[\w.]+)?', "", name)
            break
        else:
            self._name = ""

    @property
    def name(self):
        return self._name

    def draw(self, ax, coord_sys, fig_height, bracket_y, tick_h, label_y,
             trailing=False):
        """
        Draw the bracket and rotated label below the address-space rectangle.
        """
        # Vertical lines at mapping boundaries; thicker at group boundaries.
        for i, m in enumerate(self.mappings):
            x = coord_sys.compress(m.start)
            ax.plot([x, x], [0, fig_height],
                    color="black",
                    linewidth=1.0 if i == 0 else 0.5,
                    clip_on=False)

        if trailing:
            x = coord_sys.compress(self.mappings[-1].end)
            ax.plot([x, x], [0, fig_height],
                    color="black", linewidth=1.0, clip_on=False)

        if self._name:
            x_min = coord_sys.compress(self.mappings[0].start)
            x_max = coord_sys.compress(self.mappings[-1].end)
            x_center = (x_min + x_max) / 2
            ax.plot([x_min, x_min, x_max, x_max],
                    [bracket_y, bracket_y - tick_h,
                    bracket_y - tick_h, bracket_y],
                    color="black", linewidth=1.0, clip_on=False)
            ax.text(x_center, label_y, self._name,
                    ha="right", va="top", rotation=45,
                    rotation_mode="anchor")


class LegendSet:
    """
    Tracks which legend entries are used and builds the handle list.

    Call LegendSet.mark(interval) for each drawn Interval to record which
    entries are needed.  Call LegendSet.handles() to retrieve them in the
    canonical display order.
    """
    _ENTRIES = {
        (False, False): [Patch(facecolor="tab:green",
                               label="R",   alpha=0.75), False],
        (False, True):  [Patch(facecolor="tab:red",
                               label="RW",  alpha=0.75), False],
        (True,  False): [Patch(facecolor="tab:blue",
                               label="RX",  alpha=0.75), False],
        (True,  True):  [Patch(facecolor="tab:brown",
                               label="RWX", alpha=0.75), False],
        "trampoline":   [Patch(facecolor="tab:cyan",
                               label="Only via Trampoline", alpha=0.75), False],
        "sealed":       [Patch(facecolor="white", edgecolor="black",
                               hatch="////", label="Sealed"), False],
    }

    @staticmethod
    def mark(interval):
        """Record the legend entries needed by this interval."""
        if interval.all_trampoline:
            LegendSet._ENTRIES["trampoline"][1] = True
        else:
            LegendSet._ENTRIES[(interval.has_execute, interval.has_store)][1] = True
        if interval.all_sealed:
            LegendSet._ENTRIES["sealed"][1] = True

    @staticmethod
    def handles():
        """
        Return legend Patch handles for all marked entries, in display order.
        """
        return [a[0] for a in LegendSet._ENTRIES.values() if a[1]]


def compute_intervals(mappings, caps, mask_prot=False, all_mappings=False):
    """
    Compute Intervals that should be drawn.

    For each mapping, overlapping capabilities are clipped to the mapping
    boundary and all boundary points are recorded.  The permissions of each
    resulting Interval are the union of those of the capabilities that cover it,
    accumulated via Interval.union().

    A filtered list of reachable mappings is also returned.
    """
    intervals = []
    filtered = {}

    for base, ms in mappings.items():
        reachable = False

        for m in ms:
            points = {m.start, m.end}

            for cap in caps:
                if cap.base < m.end and cap.top > m.start:
                    points.add(max(cap.base, m.start))
                    points.add(min(cap.top, m.end))
                    m.overlapping_caps.append(cap)

            if m.overlapping_caps:
                reachable = True
            else:
                continue

            sorted_points = sorted(points)
            for i in range(len(sorted_points) - 1):
                seg_start = sorted_points[i]
                seg_end   = sorted_points[i + 1]

                interval = Interval(start=seg_start, end=seg_end,
                                    covering=[],
                                    has_store=False,
                                    has_execute=False,
                                    all_sealed=True,
                                    all_trampoline=True)

                for cap in m.overlapping_caps:
                    interval.union(cap)

                if mask_prot:
                    interval.has_execute = interval.has_execute and m.prot_exec
                    interval.has_store   = interval.has_store   and m.prot_write

                if interval.covering:
                    intervals.append(interval)

        if all_mappings or reachable:
            filtered[base] = ms

    return intervals, filtered


def render_figure(intervals, coord_sys, mappings,
                  fig_width=3.25, fig_height=0.5, font_size=9):
    """Render the address-space figure and return the matplotlib Figure."""
    plt.rcParams.update({
        "font.family": "serif",
        "font.serif": ["Linux Libertine", "Times"],
        "font.size": font_size,
        "hatch.linewidth": 0.5,
    })

    y_lo = -1.0                      # room for bracket labels below
    y_hi = fig_height + 0.5          # room for legend above
    fig, ax = plt.subplots(figsize=(fig_width, y_hi - y_lo))

    # Colored intervals.
    for interval in intervals:
        interval.draw(ax, coord_sys, fig_height)
        LegendSet.mark(interval)

    # Top and bottom edges.
    coord_sys.draw_edges(ax, fig_height)

    # Bracket labels.
    bracket_y = -0.025
    tick_h    =  0.025
    label_y   = bracket_y - tick_h - 0.025
    for i, ms in enumerate(mappings.values()):
        group = BaseGroup(ms)
        group.draw(ax, coord_sys, fig_height, bracket_y, tick_h, label_y,
                   trailing=(i == len(mappings) - 1))

    # Legend.
    rect_top_frac = (fig_height + 0.125 - y_lo) / (y_hi - y_lo)
    ax.legend(handles=LegendSet.handles(), loc="lower left",
              bbox_to_anchor=(0, rect_top_frac), ncol=3, columnspacing=0.75,
              borderpad=0.25, labelspacing=0.25,
              handletextpad=0.5, borderaxespad=0.0)

    ax.set_xlim(0, coord_sys.total_width)
    ax.set_ylim(y_lo, y_hi)
    ax.set_aspect("auto")
    ax.axis("off")

    return fig


def main():
    args = parse_args()
    mappings, caps = load_trace(args.trace)

    intervals, mappings = compute_intervals(
        mappings, caps,
        mask_prot=args.mask_prot,
        all_mappings=args.all_mappings)

    coord_sys = CoordSystem(mappings)

    fig = render_figure(intervals, coord_sys, mappings,
                        fig_width=args.width, fig_height=args.height,
                        font_size=args.font_size)

    if args.why_brown:
        for interval in intervals:
            if interval.color != "tab:brown":
                continue
            print(f"[{interval.start:#x}, {interval.end:#x}):")
            for cap in interval.covering:
                perms = ("R" +
                        ("W" if cap.perm_store   else "") +
                        ("X" if cap.perm_execute else ""))
                flags  = " sealed"     if cap.sealed         else ""
                flags += " trampoline" if cap.via_trampoline else ""
                print(f"  [{cap.base:#x}, {cap.top:#x}) {perms}{flags}")

    if args.output:
        fig.savefig(args.output, bbox_inches="tight", transparent=True)
        plt.close(fig)
    else:
        plt.show()


if __name__ == "__main__":
    main()
