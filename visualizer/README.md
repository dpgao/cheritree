# cheritree visualizer

`visualize.py` reads JSONL output produced by cheritree (one file of
capabilities and one of mappings) and renders a figure showing which parts of
the mapped address space are reachable via capabilities, colored by permission
type.

## Usage

```
python3 visualize.py CAPABILITIES MAPPINGS [-o OUTPUT]
```

| Argument | Description |
|---|---|
| `CAPABILITIES` | JSONL file of capabilities (one JSON object per line). |
| `MAPPINGS` | JSONL file of mappings (one JSON object per line). |
| `-o OUTPUT` | Output file.  If omitted, the figure is shown in an interactive window. |

### Examples

```sh
# Interactive window
python3 visualize.py caps.json maps.json

# PDF output
python3 visualize.py caps.json maps.json -o figure.pdf

# PGF for LaTeX inclusion
python3 visualize.py caps.json maps.json -o figure.pgf
```

## Input format

Both input files use JSONL (one JSON object per line, **not** a JSON array).

### Capabilities

Each line must contain at least these fields:

| Field | Type | Description |
|---|---|---|
| `base` | int | Base address of the capability. |
| `top` | int | Top (exclusive) address of the capability. |
| `perm_execute` | bool | Whether the capability has execute permission. |
| `perm_store` | bool | Whether the capability has store permission. |
| `sealed` | bool | Whether the capability is sealed. |
| `via_trampoline` | bool | *(Optional)* Whether the capability is reachable only via a trampoline. |

### Mappings

Each line must contain at least these fields:

| Field | Type | Description |
|---|---|---|
| `start` | int | Start address of the mapping. |
| `end` | int | End (exclusive) address of the mapping. |
| `mapping` | string | Display name (e.g. library path or `[heap]`). |
| `base` | string | Base path used to group related mappings (e.g. the ELF file). |

## How the figure is generated

The figure is produced in four phases.

### Phase 1 — Parse inputs

Capabilities are loaded as `Capability` objects.  Mappings are sorted by start
address and stored as `(start, end, name, base)` tuples.

### Phase 2 — Coordinate compression

Real address ranges span many orders of magnitude (4 KiB pages to multi-GiB
regions), so plotted widths use **log₂(region size)** (with a minimum of 1.0)
instead of the true byte count.  Each mapping is assigned a display offset and
width; the offsets are cumulative, so the mappings tile left-to-right with no
gaps.

A `compress(addr)` function maps any address within a mapping to its display
x-coordinate by linear interpolation within the compressed region.  Addresses
outside any mapping cause a `ValueError`.

### Phase 3 — Permission sweep

For each mapping the script finds all capabilities whose `[base, top)` range
overlaps the mapping.  It clips them to the mapping boundaries, collects all
boundary points, and sweeps consecutive segments.  For each segment it computes:

- **has\_execute** / **has\_store** — the OR of the corresponding permission
  across all covering capabilities.
- **all\_sealed** — true only if every covering capability is sealed.
- **all\_trampoline** — true only if every covering capability has
  `via_trampoline` set.

The result is a list of `(start, end, color, all_sealed)` intervals.

### Phase 4 — Rendering

The figure is sized so the colored rectangle is exactly 3.25 × 0.5 inches.  The
y-axis extends below the rectangle for bracket labels and above it for the
legend; the figure height is derived automatically.

| Element | Details |
|---|---|
| **Colored regions** | Each interval from Phase 3 is drawn as a rectangle. |
| **Color scheme** | Green = R (read-only), Red = RW, Blue = RX, Brown = RWX, Cyan = Only Via Trampoline. |
| **Sealed hatch** | Intervals reachable *only* via sealed capabilities get a diagonal hatch overlay. |
| **Labels** | Mappings sharing the same `base` path are grouped.  A bracket spans the group, and the mapping name (basename only, brackets stripped) is drawn below the bracket center. |
