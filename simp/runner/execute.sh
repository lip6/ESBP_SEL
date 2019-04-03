#!/bin/bash

CNF="$1"
shift

cat "$CNF" | $(dirname $0)/BreakID -no-row -no-bin -no-small -no-relaxed -s -1 -store-sym "$CNF.sym" > /dev/null
$(dirname $0)/glucose -model "$CNF" "$@"
