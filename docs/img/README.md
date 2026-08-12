# Figures

Regenerate any of these from a checkout:

```sh
D=external/janestreet-asic-puzzle
B=build/default/asicrev

$B export $D/warmup/04_final.gds -o /tmp/a.svg --colour net   --dim-power --width 2000
$B export $D/warmup/04_final.gds -o /tmp/c.svg --colour layer --width 1600 --clip 25000 35000 47000 46000
$B export $D/warmup/04_final.gds -o /tmp/d.svg --colour net --dim-power --width 1600 --clip 25000 35000 47000 46000
$B export $D/puzzle.gds          -o /tmp/e.svg --colour net   --dim-power --width 2400

rsvg-convert -w 1600 /tmp/a.svg -o layout_nets.png
rsvg-convert -w 1200 /tmp/c.svg -o zoom_layers.png
rsvg-convert -w 1200 /tmp/d.svg -o zoom_nets.png
rsvg-convert -w  760 /tmp/e.svg -o puzzle_nets.png
```
