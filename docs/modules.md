# Module notes

Why each part exists and what is subtle about it. The short version of every
module is in the [README](../README.md); this is the layer underneath, aimed at
someone about to change the code.

---

## `src/gds/` — reading the file

`reader.cpp` walks a flat sequence of length-prefixed, big-endian records and
builds a library of cells. The element subset a finished chip uses is small:
`BOUNDARY`, `PATH`, `BOX`, `SREF`/`AREF`, `TEXT`, with `STRANS`/`MAG`/`ANGLE`.

**The trap.** Real numbers are stored in IBM System/360 excess-64 hexadecimal
format, not IEEE 754. A reader that assumes IEEE 754 does not crash — it
silently mis-scales the database unit, and every coordinate is then wrong by a
factor nobody notices until much later.

`flatten.cpp` composes each placement transform down the tree and tags every
emitted shape with the standard-cell instance it came from, so a pin label
found inside a cell can be attributed to the right instance afterwards. `PATH`
elements become rectangles here, so nothing downstream ever sees a stroked line.

A referenced structure is treated as a standard cell if it is a hierarchy leaf
**and** carries at least one pin label on a routing layer. That is a property of
the technology rather than a name pattern, so via structures and filler geometry
— which carry no labels — get inlined into the routing, which is what you want.

---

## `src/extract/` — geometry to nets

Three files, in dependency order.

**`rect_decompose.cpp`** cuts a rectilinear polygon into disjoint rectangles by
sweeping the distinct y coordinates. Within a slab, no vertex occurs, so the
crossings of any horizontal line are the same set of vertical edges; pairing
them by the even-odd rule gives the covered spans. Slabs whose span sets match
are merged vertically afterwards, otherwise a long straight wire becomes one
rectangle per scanline.

Non-Manhattan polygons fall back to their bounding box. That errs toward
over-connection, so the count is reported rather than hidden.

**`connectivity.cpp`** holds the union-find and a uniform hash grid, one per
conducting level. Layouts are dense and near-uniform, which is exactly where a
grid beats a balanced tree and needs no rebalancing.

> **The asymmetry that matters.** Same-level rectangles are merged when they
> *touch*: abutting metal conducts. A via merges the shapes it *overlaps*.
> Reusing the touch test for vias shorts any two nets whose geometry abuts the
> same via boundary — common whenever a via sits at the end of a wire next to
> another track. This is the single easiest way to produce a plausible, wrong
> netlist.

**`extractor.cpp`** ties it together: flatten, decompose, merge, then bind each
cell's pin labels to the net covering them. A label landing on no shape, or a
pin whose repeated labels resolve to different nets, is a hard error and is
counted. On real designs those counters are zero, and that is the extractor's
own report that it understood the file.

---

## `src/tech/` — what a cell is

`sky130.cpp` is the layer stack: six conducting levels (li1, met1–met5) and five
cut layers. Extraction deliberately stops at li1; below it lie the contacts,
poly and diffusion that are internal to cells whose behaviour comes from a
library, so transistors never have to be recognised.

`std_cells.cpp` is that library — pin lists, directions, and a small Boolean
expression tree per output, transcribed from the process kit's own gate-level
models. Two consequences worth keeping:

- `tests/test_std_cells.cpp` re-derives each truth table and compares it against
  the published model, so a transcription slip cannot pass silently.
- The same expression trees generate the behavioural Verilog handed to external
  simulators, so there is one source of truth for what a cell does and no
  process kit installation is required.

Adding a cell means adding one line to the table. Adding a process means a new
layer stack and a new cell table; nothing else in the pipeline knows about
either.

---

## `src/netlist/` — the IR and the comparison

The IR is deliberately plain: nets, instances with named pin connections, ports.
`verilog_writer.cpp` emits it in the shape synthesis produces, so the output can
be diffed against a reference or fed straight to another tool.
`verilog_reader.cpp` reads that same subset back — it is not a general Verilog
parser and does not try to be.

**`compare.cpp`** decides whether two netlists are the same circuit up to
renaming. It builds the instance/net bipartite graph, folds pin names into the
edge labels (or the two inputs of an asymmetric gate become interchangeable),
refines colours Weisfeiler–Leman style, and then searches for an explicit
isomorphism.

> **Refine both graphs together.** Refining each separately assigns signature
> identifiers in each graph's own insertion order, so the same structural
> signature can receive different identifiers on the two sides and the
> colourings become incomparable — the check then reports a difference that does
> not exist. One shared signature table, identifiers assigned by rank in a
> canonical order.

Nets carrying the same layout name on both sides are pinned first. With that,
real netlists match with **zero** backtracking: the refinement alone is a
complete invariant at this scale.

---

## `src/sim/` — running the recovered circuit

Levelised zero-delay evaluation over three-valued logic. The combinational cloud
is topologically sorted once at construction; flip-flops are the level boundary.
Combinational loops are detected and reported rather than hanging.

> **How a clock edge is applied.** Drive the primary clock low, let the clock
> tree settle, sample every flop's D **and its own clock net**, drive the clock
> high, settle again, and trigger only the flops whose local clock net actually
> transitioned. Buffered and inverted clock branches then work with no special
> cases — which matters, because a recovered netlist has a real clock tree in it,
> not an idealised one.

---

## `src/export/` — looking at the result

Renders the extracted rectangles as SVG, coloured either by mask level or by the
electrical node the extraction assigned. The second mode is the point: it turns
the central claim of the tool into something a human can check at a glance.

`--highlight <net>` draws one net in colour and everything else in grey, which
is the fastest way to see whether a net is a tree or a mistake. `--max-level`
limits the render to the lower levels, because upper metal covers the cells
completely and an unfiltered view of any window is opaque above met2.

---

## `src/def/` — the scoreboard

Reads placement and logical connectivity out of a routed database. This is the
only module whose output is *never* used to produce a result — it exists so the
extraction can be scored against ground truth where ground truth happens to
exist.

> Positions in a routed database are the lower-left corner of a cell's abutment
> box **after** orientation; a layout stores the cell's **own origin**. For the
> flipped rows that alternate down a standard-cell layout these differ by the
> row height. Comparing raw coordinates scores 96/230 and looks like an
> extraction bug; comparing transformed abutment boxes scores 230/230.

---

## `analysis/` — the Python layer

Three scripts on top of the JSON IR, kept out of the C++ on purpose: they are
analysis, they change every ten minutes while a design is being understood, and
none of them belongs in a tool that has to stay correct.

**`pysim.py`** compiles a netlist into straight-line Python — one expression per
net, topologically ordered, `exec`'d as a single generated function. A full
121-cycle run of a 728-gate design costs about a microsecond, which is what
makes exhaustive perturbation sweeps practical.

**`recover_rtl.py`** expands each flip-flop's next-state function symbolically
and reads the register roles out of the algebra: counter moduli and bit weights
from the carry structure, shift-register taps from the `q[i]' = q[j]` chain,
accumulator widths and their target constants from the conjunction driving the
output. Emits behavioural Verilog.

**`solve_puzzle.py`** recovers the constraint structure by perturbation —
setting one input bit at a time and recording which state bits react — then
solves it and feeds the answer back through the netlist to confirm.

> **Restrict what you observe.** A perturbation disturbs everything downstream,
> including parts you do not care about. The first sweep reported twenty-seven
> constraint groups instead of eleven, because the output stage's feedback
> register and the neighbourhood shift register also react. Restricting the
> observed set to the flip-flops in the support of the output condition — which
> the symbolic expansion already gives you — fixes it.

---

## `tests/` — what is actually asserted

42 cases. The unit tests are ordinary; the useful ones are property-shaped:

| Test | Asserts |
| --- | --- |
| `test_rect_decompose` | area is conserved and pieces are pairwise disjoint, rather than matching expected output |
| `test_connectivity` | touch connects within a level, overlap is required across levels, a via stack carries a net end to end |
| `test_std_cells` | every cell's truth table, re-derived and compared against the published model |
| `test_compare` | renaming everything still compares equal; one rewired connection does not |
| `test_simulator` | async reset is level-sensitive; a buffered clock still triggers its flop |
| `test_warmup_e2e` | the entire flow on a real design: extract, compare to reference, cross-check against the routed database, simulate, sweep all 2¹⁶ inputs |

The end-to-end case is the one that matters. It means a regression fails the
build instead of quietly producing a wrong netlist.
