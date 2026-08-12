# Vendored: Jane Street ASIC reverse-engineering puzzle

These files are **not** part of asicrev. They are the input data it was built
against, copied here so the tool has something to run on out of the box.

Source: <https://blog.janestreet.com/can-you-reverse-engineer-an-asic/>
Original readme: [`PUZZLE_README.md`](PUZZLE_README.md)

Copyright remains with Jane Street. Nothing here is modified, and nothing here
is ever written to — `asicrev` only reads from this directory.

## What is here

| File | What it is | Used by asicrev for |
| --- | --- | --- |
| `warmup/00_source.v` | the original Verilog of a small design | reading, to know what the right answer is |
| `warmup/01_netlist.v` | its synthesized gate-level netlist | reference for the isomorphism and formal checks |
| `warmup/02_netlist_with_power_rails.v` | the same, with supplies connected | reference for the power-aware comparison |
| `warmup/03_post_place_and_route.def` | placement and routed connectivity | reference for the placement and net cross-check |
| `warmup/04_final.gds` | the finished layout | **the only input** to extraction |
| `puzzle.gds` | the real design's layout | the target: no reference of any kind exists |
| `example_inputs.vcd` | a recorded simulation of the real design | end-to-end check: replay it through the extracted netlist |
| `layout.png` | annotated die image naming the ports | tells you what the real design's ports are called |

The warm-up matters more than it looks. It is the same flow that produced
`puzzle.gds`, published with every intermediate artefact, which means the
extractor can be validated to the point of formal equivalence *before* being
pointed at something with no answer key.

## The ports of the real design

From `layout.png`, since the layout itself carries only pin labels:

```
  clk, rst_n, enable, I        inputs   (I is one bit, fed serially)
  success, O[7:0]              outputs
```
