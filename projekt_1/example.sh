#!/usr/bin/env bash
set -e

MAP_DIR="data/poland-latest"

if [[ -f "$MAP_DIR/nodes.bin" && \
      -f "$MAP_DIR/edges.bin" && \
      -f "$MAP_DIR/offsets.bin" && \
      -f "$MAP_DIR/cities.csv" ]]; then
    ./bin/router <<EOF
1
t
kraków
3
gdynia
1
5
EOF
else
    ./bin/router <<EOF
1
kraków
3
gdynia
1
5
EOF
fi

./bin/visualizer