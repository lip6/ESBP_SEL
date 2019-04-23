#!/bin/bash

CNF="$1"
shift

MODE="$1"
shift

# SOLVER="gdb --args $(dirname $0)/glucose"
SOLVER="valgrind --leak-check=full  --show-leak-kinds=all --track-origins=yes --leak-resolution=high $(dirname $0)/glucose_release"

# SOLVER="$(dirname $0)/glucose_release"

if [ "$MODE" = "breakid" ]; then
    cat "$CNF" | $(dirname $0)/BreakID -no-row -no-bin -no-small -no-relaxed -s -1 -store-sym "$CNF.sym" > /dev/null
    $SOLVER -model -breakid "$CNF" $@
elif [ "$MODE" = "bliss" ]; then
    $(dirname $0)/CNFBlissSymmetries "$CNF" > "$CNF.bliss"
    $SOLVER -model -bliss "$CNF" $@
else
    echo "ERROR";
fi
