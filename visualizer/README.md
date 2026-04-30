# CheriTree visualizer

`visualize.py` reads the JSON trace object produced by CheriTree and renders a
figure showing which parts of the mapped address space are reachable via
capabilities, colored by permission type.

## Usage

```
python3 visualize.py TRACE1.json [TRACE2.json ...] [-o OUTPUT] [options]
```

| Argument | Description |
|---|---|
| `TRACE*.json` | One or more JSON files containing top-level `mappings` and `capabilities` arrays. |
| `-o OUTPUT` | Output file.  If omitted, an interactive window is shown. |
| `-W INCHES` | Figure width in inches. |
| `-H INCHES` | Height of the colored rectangle in inches. |
| `-f PT` | Font size in points. |
| `--label-rotation DEG` | Label rotation in degrees. Use `0` to center labels horizontally. |
| `--why-brown` | For each RWX (brown) interval, print the covering capabilities. |
| `--mask-prot` | AND capability permissions with the mapping's protection bits. |
| `--all-mappings` | Show all mappings, even those unreachable from any capability. |
| `--edge {no,library,compart}` | Draw vertical edges for no groups, per library, or per compartment. |
| `--label {no,library,compart}` | Draw labels for no groups, per library, or per compartment. If labels are more granular than edges, they are reduced to match. |

The visualizer groups sorted mappings into contiguous `Library` objects by
stripped library name (for example, `libc` becomes `c`), and each library into
contiguous `Compart` objects by sub-library name. Compartment labels have the
form `library[:sub-library]`. Adjacent compartments with the same displayed
label share one combined label span, and compartment edges between them are
suppressed.

When multiple traces are given, the plots are stacked vertically in one figure.
The mappings from the first trace define the shared horizontal layout for every
plot, and the displayed compartments are the union of those reachable in any
input trace unless `--all-mappings` is used.

### Examples

```sh
# Interactive window
python3 visualize.py trace.json

# PDF output
python3 visualize.py trace.json -o figure.pdf --edge=library --label=library

# PGF for LaTeX inclusion
python3 visualize.py trace.json -o figure.pgf --edge=compart --label=compart

# Stack two traces using the first trace's mapping layout
python3 visualize.py trace-a.json trace-b.json -o figure.pdf --label=no

# Horizontal centered labels
python3 visualize.py trace.json --label-rotation=0
```
