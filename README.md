# CheriTree

CheriTree is a library that provides routines to print the capability graph as a tree through a depth-first search starting from the registers, which include the stack pointer and program counter capability (PCC).

## Design Notes

CheriTree is implemented as a shared library and a stub that is statically linked with the application.

The library builds an in memory list of the mapped segments and loads the associated symbol tables.
During execution, segments can change and new libraries or mappings can be added. CheriTree attempts to handle this by reloading the mapping list if necessary.

## Usage

CheriBSD 25.03 or later is required for linkage-based compartmentalisation support.

The project can be built by running `make` at the top level. Only the CheriBSD build on Morello has been tested.

To use the library with an application, link with both `libcheritreestub.a` and `libcheritree.so`. The capability tree can be printed by calling `cheritree_print()`, which is defined in `cheritree.h`.

Optionally, a call to `cheritree_init()` can be added before use. If there are multiple shared libraries, calling this from each one will enable CheriTree to identify the stack associated with each.

When using with linkage-based compartmentalisation enabled, the following environment variable __must__ be set:
~~~{.sh}
export LD_COMPARTMENT_NO_FAST_PATH=1
~~~

The root capability set can be trimmed at process startup by setting
`CHERITREE_EXCLUDE_ROOTS` to a comma separated list of register IDs.
Supported IDs are `0` through `32`, where `31` is `pcc` and `32` is `csp`.
Ranges are also supported using the syntax `start-end`.
For example:
~~~{.sh}
export CHERITREE_EXCLUDE_ROOTS=0-8,31,32
~~~

Output formatting can be controlled either programmatically or via an
environment variable.
To enable JSON output directly from code, set `cheritree_json_output`:
~~~{.c}
cheritree_json_output = 1;
~~~
Alternatively, set the `CHERITREE_JSON_OUTPUT` environment variable to any
non-empty value before the process starts:
~~~{.sh}
export CHERITREE_JSON_OUTPUT=1
~~~

By default, output goes to _stdout_. To redirect output to a file, set
`CHERITREE_OUTPUT_PATH` to a file path:
~~~{.sh}
export CHERITREE_OUTPUT_PATH=/tmp/cheritree.out
~~~

## Acknowledgements

[rtegrity](https://rtegrity.com/) would like to acknowledge the support of the [Digital Security by Design](https://www.dsbd.tech/) Technology Access Program.
