#!/bin/bash

CNF="$1"
shift

cat "$CNF" | $(dirname $0)/BreakID -store-sym "$CNF.sym" > /dev/null
$(dirname $0)/glucose -model "$CNF" "$@"
