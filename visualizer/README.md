# CheriTree visualizer

`visualize.py` reads the JSON trace object produced by CheriTree and renders a
figure showing which parts of the mapped address space are reachable via
capabilities, colored by permission type.

## Usage

```
python3 visualize.py TRACE.json [-o OUTPUT] [options]
```

| Argument | Description |
|---|---|
| `TRACE.json` | JSON file containing top-level `mappings` and `capabilities` arrays. |
| `-o OUTPUT` | Output file.  If omitted, an interactive window is shown. |
| `-W INCHES` | Figure width in inches (default: 3.25). |
| `-H INCHES` | Height of the colored rectangle in inches (default: 0.5). |
| `-f PT` | Font size in points (default: 9). |
| `--why-brown` | For each RWX (brown) interval, print the covering capabilities. |
| `--mask-prot` | AND capability permissions with the mapping's protection bits. |
| `--all-mappings` | Show all mappings, even those unreachable from any capability. |

### Examples

```sh
# Interactive window
python3 visualize.py trace.json

# PDF output
python3 visualize.py trace.json -o figure.pdf

# PGF for LaTeX inclusion
python3 visualize.py trace.json -o figure.pgf
```
