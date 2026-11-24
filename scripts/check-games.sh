#!/bin/sh
# The second screens' regression check (tools/check_games.py): builds the
# harness, then runs both games' checks against the test states. Run it
# before merging a branch that touches a second screen.
#
#   scripts/check-games.sh --alttp ROM --sm ROM [--data DIR] [--out DIR] [--only items,smmap,...]
#
# ROMs: A Link to the Past (USA) and Super Metroid (JU), as played (a
# game whose ROM isn't given is skipped). Test states: ../test-data.
set -e
cd "$(dirname "$0")/.."
make tools >/dev/null
exec python3 tools/check_games.py "$@"
