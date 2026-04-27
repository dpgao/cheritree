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
from bisect import bisect_left, bisect_right
from dataclasses import dataclass, field
from enum import IntEnum

import matplotlib.pyplot as plt
from matplotlib.patches import Patch, Rectangle


class Mode(IntEnum):
    NO = 0
    LIBRARY = 1
    COMPART = 2

    @classmethod
    def choices(cls):
        return tuple(mode.name.lower() for mode in cls)

    @classmethod
    def parse(cls, value):
        return cls[value.upper()]


def load_trace(path):
    """Load a CheriTree JSON trace object with mappings and capabilities."""
    with open(path) as f:
        trace = json.load(f)

    mappings = [Mapping.from_dict(m) for m in trace["mappings"]]
    mappings = [m for m in mappings if m.prot_read or m.prot_write or m.prot_exec]
    mappings.sort(key=lambda m: m.start)

    for c in trace["capabilities"]:
        cap = Capability.from_dict(c)
        s = bisect_right(mappings, cap.base, key=lambda m: m.end)
        e = bisect_left(mappings, cap.top, key=lambda m: m.start)
        for i in range(s, e):
            mappings[i].add_cap(cap)

    return mappings


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
    parser.add_argument("--edge", choices=Mode.choices(), default="compart",
                        help="Vertical edge granularity: none, per library, "
                             "or per compartment (default: compart).")
    parser.add_argument("--label", choices=Mode.choices(), default="compart",
                        help="Label granularity: none, per library, or per "
                             "compartment (default: compart).")
    args = parser.parse_args()
    args.edge = Mode.parse(args.edge)
    args.label = Mode.parse(args.label)
    if args.label > args.edge:
        args.label = args.edge
    return args


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


class LegendSet:
    """
    Tracks which legend entries are used and builds the handle list.

    Call LegendSet.mark(interval) for each drawn Interval to record which
    entries are needed. Call LegendSet.handles() to retrieve them in the
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
    def reset():
        """Clear all recorded legend usage."""
        for entry in LegendSet._ENTRIES.values():
            entry[1] = False

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
    intervals: list = field(default_factory=list)

    @classmethod
    def from_dict(cls, m):
        return cls(start=m["start"], end=m["end"],
                   name=m["mapping"], base=m.get("base", m["start"]),
                   prot_read=m.get("prot_read", True),
                   prot_write=m.get("prot_write", True),
                   prot_exec=m.get("prot_exec", True))

    def add_cap(self, cap):
        self.overlapping_caps.append(cap)

    def compute_intervals(self, mask_prot):
        """Compute and store the intervals covered by capabilities."""
        if not self.overlapping_caps:
            return False

        points = {self.start, self.end}
        for cap in self.overlapping_caps:
            points.add(max(cap.base, self.start))
            points.add(min(cap.top, self.end))

        sorted_points = sorted(points)
        for i in range(len(sorted_points) - 1):
            seg_start = sorted_points[i]
            seg_end = sorted_points[i + 1]

            interval = Interval(start=seg_start, end=seg_end,
                                covering=[],
                                has_store=mask_prot and self.prot_write,
                                has_execute=mask_prot and self.prot_exec,
                                all_sealed=True,
                                all_trampoline=True)

            for cap in self.overlapping_caps:
                interval.union(cap)

            if interval.covering:
                self.intervals.append(interval)

        return True

    def iter_intervals(self):
        yield from self.intervals

    def draw(self, ax, coord_sys, rect_h):
        """Draw all intervals that belong to this mapping."""
        for interval in self.intervals:
            interval.draw(ax, coord_sys, rect_h)
            LegendSet.mark(interval)


def draw_bracket_label(ax, x_min, x_max, text, bracket_y, tick_h, label_y):
    """Draw a bracket spanning [x_min, x_max] and a rotated label."""
    if not text:
        return

    x_center = (x_min + x_max) / 2
    ax.plot([x_min, x_min, x_max, x_max],
            [bracket_y, bracket_y - tick_h,
             bracket_y - tick_h, bracket_y],
            color="black", linewidth=1.0, clip_on=False)
    ax.text(x_center, label_y, text,
            ha="right", va="top", rotation=45,
            rotation_mode="anchor")


@dataclass
class Compart:
    """A contiguous sequence of mappings from one compartment."""
    library: "Library"
    name: str
    mappings: list = field(default_factory=list)

    @property
    def label(self):
        if self.library.name and self.name:
            return f"{self.library.name}:{self.name}"
        if self.name:
            return self.name
        return self.library.name

    def compute_intervals(self, mask_prot):
        """Compute intervals for each mapping and report reachability."""
        reachable = False
        for mapping in self.mappings:
            if mapping.compute_intervals(mask_prot):
                reachable = True
        return reachable

    def iter_intervals(self):
        for mapping in self.mappings:
            yield from mapping.iter_intervals()

    def draw(self, ax, coord_sys, rect_h):
        """Draw all intervals in this compartment."""
        for mapping in self.mappings:
            mapping.draw(ax, coord_sys, rect_h)

    def draw_edges(self, ax, coord_sys, fig_height, edge_mode, trailing):
        """Draw vertical edges at this compartment's boundaries."""
        if edge_mode != Mode.COMPART:
            return

        x = coord_sys.compress(self.mappings[0].start)
        ax.plot([x, x], [0, fig_height],
                color="black", linewidth=1.0, clip_on=False)

        if trailing:
            x = coord_sys.compress(self.mappings[-1].end)
            ax.plot([x, x], [0, fig_height],
                    color="black", linewidth=1.0, clip_on=False)

    def draw_labels(self, ax, coord_sys, bracket_y, tick_h, label_y, label_mode):
        """Draw the bracket and rotated label for this compartment."""
        if label_mode != Mode.COMPART:
            return

        draw_bracket_label(ax,
                           coord_sys.compress(self.mappings[0].start),
                           coord_sys.compress(self.mappings[-1].end),
                           self.label,
                           bracket_y, tick_h, label_y)


@dataclass
class Library:
    """A contiguous sequence of mappings from one library stem."""
    name: str
    comparts: list = field(default_factory=list)

    def add_mapping(self, mapping, compart_name):
        if not self.comparts or self.comparts[-1].name != compart_name:
            self.comparts.append(Compart(library=self,
                                         name=compart_name))
        self.comparts[-1].mappings.append(mapping)

    def compute_intervals(self, mask_prot, all_mappings):
        """Compute intervals for child compartments and filter by reachability."""
        reachable = False
        filtered = []

        for compart in self.comparts:
            compart_reachable = compart.compute_intervals(mask_prot)
            if compart_reachable:
                reachable = True
            if compart_reachable or all_mappings:
                filtered.append(compart)

        self.comparts = filtered
        return reachable

    def iter_intervals(self):
        for compart in self.comparts:
            yield from compart.iter_intervals()

    def draw(self, ax, coord_sys, rect_h):
        """Draw all intervals in this library."""
        for compart in self.comparts:
            compart.draw(ax, coord_sys, rect_h)

    def draw_edges(self, ax, coord_sys, fig_height, edge_mode, trailing):
        """Draw library or compartment edges for this library."""
        for i, compart in enumerate(self.comparts):
            compart.draw_edges(ax, coord_sys, fig_height, edge_mode,
                               trailing=trailing and i + 1 == len(self.comparts))

        if edge_mode == Mode.LIBRARY:
            x = coord_sys.compress(self.comparts[0].mappings[0].start)
            ax.plot([x, x], [0, fig_height],
                    color="black", linewidth=1.0, clip_on=False)

            if trailing:
                x = coord_sys.compress(self.comparts[-1].mappings[-1].end)
                ax.plot([x, x], [0, fig_height],
                        color="black", linewidth=1.0, clip_on=False)

    def draw_labels(self, ax, coord_sys, bracket_y, tick_h, label_y, label_mode):
        """Draw library or compartment labels for this library."""
        for compart in self.comparts:
            compart.draw_labels(ax, coord_sys, bracket_y, tick_h, label_y, label_mode)

        if label_mode == Mode.LIBRARY:
            draw_bracket_label(ax,
                               coord_sys.compress(self.comparts[0].mappings[0].start),
                               coord_sys.compress(self.comparts[-1].mappings[-1].end),
                               self.name,
                               bracket_y, tick_h, label_y)


def split_mapping_name(name):
    """Split a mapping name into a stripped library stem and sub-library name."""
    path, compart = name, ""
    if ":" in name:
        path, compart = name.rsplit(":", 1)
    path = path.rsplit("/", 1)[-1]
    stem = re.sub(r'\.so(?:\.[\w.]+)?$', "", path)
    if stem.startswith("lib"):
        stem = stem[3:]
    if stem.startswith("private"):
        stem = stem[7:]
    return stem, compart


class CoordSystem:
    """
    Transform addresses to x-coordinates.

    Each mapping's display width is log2(region size), clamped to at least 1.0,
    so that both tiny and huge regions are visible.
    """
    def __init__(self, mappings):
        self.libraries = []
        for mapping in mappings:
            library_name, compart_name = split_mapping_name(mapping.name)
            if not self.libraries or self.libraries[-1].name != library_name:
                self.libraries.append(Library(name=library_name))
            self.libraries[-1].add_mapping(mapping, compart_name)

    def compute_intervals(self, mask_prot, all_mappings):
        """
        Compute intervals through the hierarchy and filter unreachable nodes.
        """
        filtered = []
        for library in self.libraries:
            if library.compute_intervals(mask_prot, all_mappings):
                filtered.append(library)
        self.libraries = filtered
        self._build_layout()

    def iter_intervals(self):
        for library in self.libraries:
            yield from library.iter_intervals()

    def _build_layout(self):
        """Recompute compressed layout tables from the current libraries."""
        self.map_starts = []
        self.map_ends = []
        self.display_offsets = []
        self.display_widths = []
        offset = 0.0

        for library in self.libraries:
            for compart in library.comparts:
                for m in compart.mappings:
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

    def draw_outline(self, ax, rect_h):
        """Draw the top and bottom horizontal edges of the address-space box."""
        ax.plot([0, self.total_width], [0, 0],
                color="black", linewidth=1.0, clip_on=False)
        ax.plot([0, self.total_width], [rect_h, rect_h],
                color="black", linewidth=1.0, clip_on=False)

    def draw_sides(self, ax, rect_h):
        """Draw the left and right vertical sides of the address-space box."""
        ax.plot([0, 0], [0, rect_h],
                color="black", linewidth=1.0, clip_on=False)
        ax.plot([self.total_width, self.total_width], [0, rect_h],
                color="black", linewidth=1.0, clip_on=False)

    def draw(self, ax, rect_h, edge_mode, label_mode):
        """Draw mappings, the address-space outline, and group annotations."""
        bracket_y = -0.025
        tick_h = 0.025
        label_y = bracket_y - tick_h - 0.025
        for i, library in enumerate(self.libraries):
            library.draw(ax, self, rect_h)
            library.draw_edges(ax, self, rect_h, edge_mode,
                               trailing=i + 1 == len(self.libraries))
            library.draw_labels(ax, self, bracket_y, tick_h, label_y, label_mode)

        self.draw_outline(ax, rect_h)
        if edge_mode == Mode.NO:
            self.draw_sides(ax, rect_h)

    def render_figure(self, fig_width, fig_height, font_size, edge_mode, label_mode):
        """Render the address-space figure and return the matplotlib Figure."""
        plt.rcParams.update({
            "font.family": "serif",
            "font.serif": ["Linux Libertine", "Times"],
            "font.size": font_size,
            "hatch.linewidth": 0.5,
        })

        LegendSet.reset()

        y_lo = -1.0 if label_mode >= Mode.LIBRARY else 0.0
        y_hi = fig_height + 0.5          # room for legend above
        fig, ax = plt.subplots(figsize=(fig_width, y_hi - y_lo))

        self.draw(ax, fig_height, edge_mode, label_mode)

        rect_top_frac = (fig_height + 0.125 - y_lo) / (y_hi - y_lo)
        ax.legend(handles=LegendSet.handles(), loc="lower left",
                  bbox_to_anchor=(0, rect_top_frac), ncol=3, columnspacing=0.75,
                  borderpad=0.25, labelspacing=0.25,
                  handletextpad=0.5, borderaxespad=0.0)

        ax.set_xlim(0, self.total_width)
        ax.set_ylim(y_lo, y_hi)
        ax.set_aspect("auto")
        ax.axis("off")

        return fig


def main():
    args = parse_args()
    mappings = load_trace(args.trace)

    coord_sys = CoordSystem(mappings)
    coord_sys.compute_intervals(mask_prot=args.mask_prot,
                                all_mappings=args.all_mappings)
    fig = coord_sys.render_figure(fig_width=args.width,
                                  fig_height=args.height,
                                  font_size=args.font_size,
                                  edge_mode=args.edge,
                                  label_mode=args.label)

    if args.why_brown:
        for interval in coord_sys.iter_intervals():
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
