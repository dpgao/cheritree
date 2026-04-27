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
| `--edge {no,library,compart}` | Draw vertical edges for no groups, per library, or per compartment (default: `compart`). |
| `--label {no,library,compart}` | Draw labels for no groups, per library, or per compartment. If labels are more granular than edges, they are reduced to match. |

The visualizer groups sorted mappings into contiguous `Library` objects by
stripped library name (for example, `libc` becomes `c`), and each library into
contiguous `Compart` objects by sub-library name. Compartment labels have the
form `library[:sub-library]`.

### Examples

```sh
# Interactive window
python3 visualize.py trace.json

# PDF output
python3 visualize.py trace.json -o figure.pdf --edge=library --label=library

# PGF for LaTeX inclusion
python3 visualize.py trace.json -o figure.pgf --edge=compart --label=compart
```
