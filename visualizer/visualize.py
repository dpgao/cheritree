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
from abc import ABC, abstractmethod
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


class Segment(ABC):
    """A drawable hierarchy node laid out from left to right."""

    is_reachable = False

    @property
    @abstractmethod
    def children(self):
        """Return this segment's children in display order."""

    @property
    def x0(self):
        return children[0].x0 if (children := self.children) else 0.0

    @property
    def x1(self):
        return children[-1].x1 if (children := self.children) else 0.0

    def compute_intervals(self, mask_prot):
        self.is_reachable = False
        for child in self.children:
            if child.compute_intervals(mask_prot):
                self.is_reachable = True
        return self.is_reachable

    def iter_intervals(self):
        for child in self.children:
            yield from child.iter_intervals()

    def compute_layout(self, x0):
        cursor = x0
        for child in self.children:
            cursor += child.compute_layout(cursor)
        return cursor - x0

    def draw(self, ax, rect_h):
        for child in self.children:
            child.draw(ax, rect_h)


@dataclass
class Capability:

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
class Interval(Segment):

    _COLORS = {
        (False, False): "tab:green",   # R
        (True,  False): "tab:red",     # RW
        (False, True):  "tab:blue",    # RX
        (True,  True):  "tab:brown",   # RWX
    }

    start: int
    end: int
    covering: list
    has_store: bool
    has_execute: bool
    all_sealed: bool
    all_trampoline: bool
    layout_width: float = field(init=False, default=0.0)
    _x0: float = field(init=False, default=0.0)
    _x1: float = field(init=False, default=0.0)

    @property
    def children(self):
        return ()

    @property
    def x0(self):
        return self._x0

    @property
    def x1(self):
        return self._x1

    @property
    def color(self):
        if self.all_trampoline:
            return "tab:cyan"
        return Interval._COLORS[(self.has_store, self.has_execute)]

    @property
    def covered(self):
        return bool(self.covering)

    def compute_layout(self, x0):
        """Store this interval's display coordinates and return its width."""
        self._x0 = x0
        self._x1 = x0 + self.layout_width
        return self.layout_width

    def draw(self, ax, rect_h):
        if not self.covered:
            return

        LegendSet.mark(self)

        ax.add_patch(Rectangle((self.x0, 0), self.x1 - self.x0, rect_h,
                               facecolor=self.color,
                               edgecolor="none",
                               alpha=0.75))
        if self.all_sealed:
            ax.add_patch(Rectangle((self.x0, 0), self.x1 - self.x0, rect_h,
                                   facecolor="none", hatch="////"))

    def iter_intervals(self):
        yield self

    def union(self, cap):
        if cap.base <= self.start and self.end <= cap.top:
            self.covering.append(cap)
            self.has_store      = self.has_store      or  cap.perm_store
            self.has_execute    = self.has_execute    or  cap.perm_execute
            self.all_sealed     = self.all_sealed     and cap.sealed
            self.all_trampoline = self.all_trampoline and cap.via_trampoline

class LegendSet:

    _ENTRIES = {
        (False, False): [Patch(facecolor="tab:green",
                               label="R", alpha=0.75), False],
        (True,  False): [Patch(facecolor="tab:red",
                               label="RW", alpha=0.75), False],
        (False, True):  [Patch(facecolor="tab:blue",
                               label="RX", alpha=0.75), False],
        (True,  True):  [Patch(facecolor="tab:brown",
                               label="RWX", alpha=0.75), False],
        "trampoline":   [Patch(facecolor="tab:cyan",
                               label="Via Trampoline", alpha=0.75), False],
        "sealed":       [Patch(facecolor="white", edgecolor="black",
                               hatch="////", label="Sealed"), False],
    }

    @staticmethod
    def reset():
        for entry in LegendSet._ENTRIES.values():
            entry[1] = False

    @staticmethod
    def mark(interval):
        """Record the legend entries needed by this interval."""
        if interval.all_trampoline:
            LegendSet._ENTRIES["trampoline"][1] = True
        else:
            LegendSet._ENTRIES[(interval.has_store, interval.has_execute)][1] = True
        if interval.all_sealed:
            LegendSet._ENTRIES["sealed"][1] = True

    @staticmethod
    def handles():
        return [a[0] for a in LegendSet._ENTRIES.values() if a[1]]


@dataclass
class Mapping(Segment):

    start: int
    end: int
    name: str
    base: int
    prot_read: bool
    prot_write: bool
    prot_exec: bool
    overlapping_caps: list = field(default_factory=list)
    intervals: list = field(default_factory=list)

    @property
    def children(self):
        return self.intervals

    @classmethod
    def from_dict(cls, m):
        return cls(start=m["start"], end=m["end"],
                   name=m["mapping"], base=m.get("base", m["start"]),
                   prot_read=m.get("prot_read", True),
                   prot_write=m.get("prot_write", True),
                   prot_exec=m.get("prot_exec", True))

    def clone(self):
        return Mapping(start=self.start, end=self.end,
                       name=self.name, base=self.base,
                       prot_read=self.prot_read,
                       prot_write=self.prot_write,
                       prot_exec=self.prot_exec)

    def add_cap(self, cap):
        self.overlapping_caps.append(cap)

    def compute_intervals(self, mask_prot):
        points = {self.start, self.end}
        for cap in self.overlapping_caps:
            points.add(max(cap.base, self.start))
            points.add(min(cap.top, self.end))

        self.intervals = []
        self.is_reachable = False

        sorted_points = sorted(points)
        for i in range(len(sorted_points) - 1):
            seg_start = sorted_points[i]
            seg_end = sorted_points[i + 1]

            interval = Interval(start=seg_start, end=seg_end,
                                covering=[],
                                has_store=False,
                                has_execute=False,
                                all_sealed=True,
                                all_trampoline=True)

            for cap in self.overlapping_caps:
                interval.union(cap)

            if mask_prot:
                interval.has_store   = interval.has_store   and self.prot_write
                interval.has_execute = interval.has_execute and self.prot_exec

            self.intervals.append(interval)

            if interval.covered:
                self.is_reachable = True

        return self.is_reachable


def draw_bracket_label(ax, x0, x1, text, bracket_y, tick_h, label_y,
                       label_rotation):
    """Draw a bracket spanning [x0, x1] and a label below it."""
    if not text:
        return

    x_center = (x0 + x1) / 2
    ax.plot([x0, x0, x1, x1],
            [bracket_y, bracket_y - tick_h, bracket_y - tick_h, bracket_y],
            color="black", linewidth=1.0, clip_on=False)
    if label_rotation == 0:
        ax.text(x_center, label_y, text, ha="center", va="top")
    else:
        ax.text(x_center, label_y, text, ha="right", va="top",
                rotation=label_rotation, rotation_mode="anchor")


@dataclass
class Compart(Segment):

    library: "Library"
    name: str
    mappings: list = field(default_factory=list)

    @property
    def children(self):
        return self.mappings

    @property
    def label(self):
        if self.name:
            return self.name
        return self.library.name

    def compute_layout(self, x0):
        """
        Assign transformed widths to this compartment's leaf intervals.

        The compartment as a whole keeps display width `log2(total bytes)`, but
        that width is redistributed across child intervals according to
        `log2(interval bytes)`. Once those leaf widths are set,
        `Segment.compute_layout()` places the intervals left-to-right through
        the mapping hierarchy.
        """
        total_width = 0
        total_weight = 0.0
        pairs = []
        for mapping in self.mappings:
            for interval in mapping.intervals:
                width = interval.end - interval.start
                total_width += width

                weight = math.log2(width)
                total_weight += weight

                pairs.append((interval, weight))

        normalizer = math.log2(total_width) / total_weight

        for interval, weight in pairs:
            interval.layout_width = weight * normalizer

        return super().compute_layout(x0)

@dataclass
class Library(Segment):

    name: str
    comparts: list = field(default_factory=list)

    @property
    def children(self):
        return self.comparts

    def add_mapping(self, mapping, compart_name):
        if not compart_name or not self.comparts or self.comparts[-1].name != compart_name:
            self.comparts.append(Compart(library=self, name=compart_name))
        self.comparts[-1].mappings.append(mapping)

    def draw_edges(self, ax, fig_height, edge_mode, trailing):
        if edge_mode >= Mode.LIBRARY:
            ax.plot([self.x0, self.x0], [0, fig_height],
                    color="black", linewidth=1.0, clip_on=False)
            if trailing:
                ax.plot([self.x1, self.x1], [0, fig_height],
                        color="black", linewidth=1.0, clip_on=False)

        if edge_mode == Mode.COMPART:
            for i in range(1, len(self.comparts)):
                if self.comparts[i - 1].label == self.comparts[i].label:
                    continue
                ax.plot([self.comparts[i].x0, self.comparts[i].x0],
                        [0, fig_height],
                        color="black", linewidth=1.0, clip_on=False)

    def draw_labels(self, ax, bracket_y, tick_h, label_y,
                    label_mode, label_rotation):
        if label_mode == Mode.COMPART:
            run_start = 0
            while run_start < len(self.comparts):
                label = self.comparts[run_start].label
                run_end = run_start + 1
                while (run_end < len(self.comparts) and
                       self.comparts[run_end].label == label):
                    run_end += 1

                draw_bracket_label(ax,
                                   self.comparts[run_start].x0,
                                   self.comparts[run_end - 1].x1,
                                   label,
                                   bracket_y, tick_h, label_y,
                                   label_rotation)
                run_start = run_end
            return

        if label_mode == Mode.LIBRARY:
            draw_bracket_label(ax, self.x0, self.x1, self.name,
                               bracket_y, tick_h, label_y,
                               label_rotation)


def split_mapping_name(name):
    """Split a mapping name into library name and sub-library name."""
    if name.startswith("[") and name.endswith("]"):
        name = name[1:-1]
    stem, compart = name, ""
    if ":" in name:
        stem, compart = name.rsplit(":", 1)
    stem = stem.rsplit("/", 1)[-1]
    if stem.startswith("lib"):
        stem = stem[3:]
    if stem.startswith("private"):
        stem = stem[7:]
    stem = re.sub(r"\.so(?:\.[\w.]+)?$", "", stem)
    return stem, compart


class CoordSystem(Segment):

    def __init__(self, mappings):
        self.name = "top"
        self.libraries = []
        for mapping in mappings:
            library_name, compart_name = split_mapping_name(mapping.name)
            if not library_name or not self.libraries or self.libraries[-1].name != library_name:
                self.libraries.append(Library(name=library_name))
            self.libraries[-1].add_mapping(mapping, compart_name)

    @property
    def children(self):
        return self.libraries

    def filter_comparts(self, visible_comparts):
        filtered_libraries = []
        for library_index, library in enumerate(self.libraries):
            filtered_comparts = []
            for compart_index, compart in enumerate(library.comparts):
                if (library_index, compart_index) in visible_comparts:
                    filtered_comparts.append(compart)
            library.comparts = filtered_comparts
            if library.comparts:
                filtered_libraries.append(library)
        self.libraries = filtered_libraries

    def draw_outline(self, ax, rect_h):
        """Draw the top and bottom edges of the address-space box."""
        ax.plot([self.x0, self.x1], [0, 0],
                color="black", linewidth=1.0, clip_on=False)
        ax.plot([self.x0, self.x1], [rect_h, rect_h],
                color="black", linewidth=1.0, clip_on=False)

    def draw_sides(self, ax, rect_h):
        """Draw the left and right edges of the address-space box."""
        ax.plot([self.x0, self.x0], [0, rect_h],
                color="black", linewidth=1.0, clip_on=False)
        ax.plot([self.x1, self.x1], [0, rect_h],
                color="black", linewidth=1.0, clip_on=False)

    def draw(self, ax, rect_h, edge_mode, label_mode, label_rotation):
        bracket_y = -0.025
        tick_h = 0.025
        label_y = bracket_y - tick_h - 0.025
        for i, library in enumerate(self.libraries):
            library.draw(ax, rect_h)
            library.draw_edges(ax, rect_h, edge_mode,
                               trailing=i + 1 == len(self.libraries))
            library.draw_labels(ax, bracket_y, tick_h, label_y,
                                label_mode, label_rotation)

        self.draw_outline(ax, rect_h)
        if edge_mode == Mode.NO:
            self.draw_sides(ax, rect_h)

def load_trace(path, template_mappings=None):
    with open(path) as trace_file:
        trace = json.load(trace_file)

    if template_mappings is None:
        mappings = [Mapping.from_dict(mapping) for mapping in trace["mappings"]]
        mappings = [mapping for mapping in mappings
                    if mapping.prot_read or mapping.prot_write or mapping.prot_exec]
        mappings.sort(key=lambda mapping: mapping.start)
    else:
        mappings = [mapping.clone() for mapping in template_mappings]

    for cap_data in trace["capabilities"]:
        cap = Capability.from_dict(cap_data)
        start = bisect_right(mappings, cap.base, key=lambda mapping: mapping.end)
        end = bisect_left(mappings, cap.top, key=lambda mapping: mapping.start)
        for index in range(start, end):
            mappings[index].add_cap(cap)

    return mappings


def collect_visible_comparts(coord_systems):
    visible_comparts = set()
    for coord_sys in coord_systems:
        for library_index, library in enumerate(coord_sys.libraries):
            for compart_index, compart in enumerate(library.comparts):
                if compart.is_reachable:
                    visible_comparts.add((library_index, compart_index))
    return visible_comparts


def render_figure(coord_systems, fig_width, fig_height, font_size,
                  edge_mode, label_mode, label_rotation):
    plt.rcParams.update({
        "font.family": "serif",
        "font.serif": ["Linux Libertine", "Times"],
        "font.size": font_size,
        "hatch.linewidth": 0.5,
    })

    LegendSet.reset()

    height_ratios = []
    for index in range(len(coord_systems)):
        axis_height = fig_height
        if index == 0:
            axis_height += 0.5
        if index + 1 == len(coord_systems) and label_mode > Mode.NO:
            axis_height += 1.0
        height_ratios.append(axis_height)

    fig, axes = plt.subplots(len(coord_systems), 1,
                             figsize=(fig_width, sum(height_ratios)),
                             squeeze=False,
                             gridspec_kw={"height_ratios": height_ratios,
                                          "hspace": 0.0})
    axes = [axis[0] for axis in axes]

    for index, (ax, coord_sys) in enumerate(zip(axes, coord_systems)):
        current_label_mode = label_mode if index + 1 == len(coord_systems) else Mode.NO
        y_lo = -1.0 if current_label_mode > Mode.NO else 0.0
        y_hi = fig_height + 0.5 if index == 0 else fig_height

        coord_sys.draw(ax, fig_height,
                       edge_mode, current_label_mode, label_rotation)

        ax.set_xlim(coord_systems[0].x0, coord_systems[0].x1)
        ax.set_ylim(y_lo, y_hi)
        ax.set_aspect("auto")
        ax.axis("off")

    top_y_lo = -1.0 if len(coord_systems) == 1 and label_mode > Mode.NO else 0.0
    top_y_hi = fig_height + 0.5
    rect_top_frac = (fig_height + 0.125 - top_y_lo) / (top_y_hi - top_y_lo)
    axes[0].legend(handles=LegendSet.handles(), loc="lower left",
                   bbox_to_anchor=(0, rect_top_frac), ncol=3,
                   columnspacing=0.75, borderpad=0.25, labelspacing=0.25,
                   handletextpad=0.5, borderaxespad=0.0)

    return fig


def parse_args():
    parser = argparse.ArgumentParser(
        description="Visualize CHERI capability address space reachability.")
    parser.add_argument("traces", nargs="+",
                        help="CheriTree JSON trace files containing mappings "
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
    parser.add_argument("--label-rotation", type=float, default=45,
                        metavar="DEG",
                        help="Label rotation in degrees (default: 45). "
                             "Use 0 to center labels horizontally.")
    args = parser.parse_args()
    args.edge = Mode.parse(args.edge)
    args.label = Mode.parse(args.label)
    if args.label > args.edge:
        args.label = args.edge
    return args


def main():
    args = parse_args()
    template_mappings = load_trace(args.traces[0])
    coord_systems = [CoordSystem(template_mappings)]
    for trace_path in args.traces[1:]:
        coord_systems.append(CoordSystem(load_trace(trace_path, template_mappings)))

    for coord_sys in coord_systems:
        coord_sys.compute_intervals(mask_prot=args.mask_prot)

    if not args.all_mappings:
        visible_comparts = collect_visible_comparts(coord_systems)
        for coord_sys in coord_systems:
            coord_sys.filter_comparts(visible_comparts)

    for coord_sys in coord_systems:
        coord_sys.compute_layout(0.0)

    fig = render_figure(coord_systems,
                        fig_width=args.width,
                        fig_height=args.height,
                        font_size=args.font_size,
                        edge_mode=args.edge,
                        label_mode=args.label,
                        label_rotation=args.label_rotation)

    if args.why_brown:
        for coord_sys in coord_systems:
            for interval in coord_sys.iter_intervals():
                if not interval.covered or interval.color != "tab:brown":
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
        fig.savefig(args.output, bbox_inches="tight", transparent=True, dpi=600)
        plt.close(fig)
    else:
        plt.show()


if __name__ == "__main__":
    main()
