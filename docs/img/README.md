# Figures

Each filename says which layout it came from. `puzzle_*` is `puzzle.gds`,
`warmup_*` is `warmup/04_final.gds`. Do not mix them up in the README: they are
different designs, and a picture of the wrong one silently misrepresents a
result.

| File | Source layout | Command |
| --- | --- | --- |
| `puzzle_nets.png` | `puzzle.gds` | `--colour net --dim-power` |
| `puzzle_input.png` | `puzzle.gds` | `--highlight I --no-instances` |
| `warmup_nets.png` | `warmup/04_final.gds` | `--colour net --dim-power` |
| `warmup_zoom_layers.png` | `warmup/04_final.gds` | `--colour layer --clip …` |
| `warmup_zoom_nets.png` | `warmup/04_final.gds` | `--colour net --dim-power --clip …` |

Regenerate all of them from a checkout:

```sh
D=external/janestreet-asic-puzzle
B=build/default/asicrev
T=$(mktemp -d)

$B export $D/puzzle.gds -o $T/p_nets.svg  --colour net --dim-power --width 2400
$B export $D/puzzle.gds -o $T/p_input.svg --highlight I --no-instances --width 2400
$B export $D/warmup/04_final.gds -o $T/w_nets.svg --colour net --dim-power --width 2000
$B export $D/warmup/04_final.gds -o $T/w_zl.svg \
    --colour layer --width 1600 --clip 25000 35000 47000 46000
$B export $D/warmup/04_final.gds -o $T/w_zn.svg \
    --colour net --dim-power --width 1600 --clip 25000 35000 47000 46000

rsvg-convert -w  760 $T/p_nets.svg  -o docs/img/puzzle_nets.png
rsvg-convert -w  900 $T/p_input.svg -o docs/img/puzzle_input.png
rsvg-convert -w 1600 $T/w_nets.svg  -o docs/img/warmup_nets.png
rsvg-convert -w 1200 $T/w_zl.svg    -o docs/img/warmup_zoom_layers.png
rsvg-convert -w 1200 $T/w_zn.svg    -o docs/img/warmup_zoom_nets.png
```
