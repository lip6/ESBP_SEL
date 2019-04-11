#!/bin/bash

CNF="$1"
shift

MODE="$1"
shift


if [ "$MODE" = "breakid" ]; then
    cat "$CNF" | $(dirname $0)/BreakID -no-row -no-bin -no-small -no-relaxed -s -1 -store-sym "$CNF.sym" > /dev/null
    $(dirname $0)/glucose_release -model -breakid "$CNF" $@
elif [ "$MODE" = "bliss" ]; then
    $(dirname $0)/CNFBlissSymmetries "$CNF" > "$CNF.bliss"
    $(dirname $0)/glucose_release -model -bliss "$CNF" $@
else
    echo "ERROR";
fi
