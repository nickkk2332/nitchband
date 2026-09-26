#!/bin/bash
# Monster AI regression test.
#
# Plays a fixed list of seeded AI experiments through the wizard-mode test
# harness (^A R duels, ^A K kite tests) in a real curses game inside tmux,
# and compares each run's RNG fingerprint with tests/ai_expected.txt. Any
# change to monster decisions shifts the random sequence, so a changed
# fingerprint means "AI behaviour changed": either a bug, or an intended
# change that needs the expected file updated (run with --update).
#
# usage: tests/ai_regress.sh [--update] [path/to/frogcomposband]
# needs: tmux. Runs the game against a temporary copy of lib/, so the
# working tree is never touched. The fixture is a level 1 hobbit rogue
# standing in town (tests/fixture/Parity.sav); regenerate it if the
# savefile format changes.

set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
UPDATE=0
if [ "${1:-}" = "--update" ]; then UPDATE=1; shift; fi
BIN=${1:-$ROOT/src/frogcomposband}
EXPECTED=$ROOT/tests/ai_expected.txt
TIMEOUT=${AI_TEST_TIMEOUT:-600}

[ -x "$BIN" ] || { echo "no game binary at $BIN"; exit 2; }
command -v tmux >/dev/null || { echo "tmux is required"; exit 2; }

WORK=$(mktemp -d)
SESS=aitest$$
trap 'tmux kill-session -t $SESS 2>/dev/null; rm -rf "$WORK"' EXIT
cp -r "$ROOT/lib" "$WORK/lib"
L=$WORK/lib
echo "Y:allow_debug_opts" >> "$L/user/user.prf"
cp "$ROOT/tests/fixture/Parity.sav" "$L/save/$(id -u).Parity"

pane() { tmux capture-pane -p -t $SESS; }

# wait_for <text> [seconds]: poll the screen until it shows <text>
wait_for() {
    local t=0 limit=${2:-30}
    while ! pane | grep -qF -- "$1"; do
        sleep 0.2
        t=$((t + 1))
        if [ $t -ge $((limit * 5)) ]; then
            echo "timed out waiting for: $1" >&2
            pane | grep -v '^\s*$' | tail -5 >&2
            return 1
        fi
    done
}

# answer <prompt> <value>: wait for a prompt, replace its default, send
answer() {
    wait_for "$1" || return 1
    tmux send-keys -t $SESS C-u
    tmux send-keys -t $SESS -l "$2"
    tmux send-keys -t $SESS Enter
}

tmux new-session -d -s $SESS -x 110 -y 70 \
    "cd $WORK && '$BIN' -mgcu -uParity -da=$L/apex/ -db=$L/bone/ -dd=$L/data/ -de=$L/edit/ -df=$L/file/ -dh=$L/help/ -di=$L/info/ -ds=$L/save/ -du=$L/user/ -dx=$L/xtra/; sleep 5"
wait_for "Any Other Key" 60 || { echo "game did not start"; exit 2; }
tmux send-keys -t $SESS Enter
wait_for "LEVEL" 60 || { echo "game did not load the fixture"; exit 2; }
tmux send-keys -t $SESS Escape

pass=0; fail=0
NEW=$WORK/expected.new
: > "$NEW"
while IFS= read -r line; do
    case "$line" in ''|'#'*) echo "$line" >> "$NEW"; continue;; esac
    IFS='|' read -r kind a b c d e f g h want <<< "$line"
    tmux send-keys -t $SESS Escape
    sleep 0.3
    tmux send-keys -t $SESS C-a
    wait_for "Debug Command" 10 || { echo "no debug prompt"; exit 2; }
    if [ "$kind" = R ]; then
        # R|monster A|monster B|fights|seed|||||fingerprint
        tmux send-keys -t $SESS R
        answer "irst monster" "$a" && answer "econd monster" "$b" &&
        answer "umber of fights" "$c" && answer "andom seed" "$d" || exit 2
        label="duel $a vs $b"
    else
        # K|monster|trials|turns|mode|seed|group|profile||fingerprint
        tmux send-keys -t $SESS K
        answer "Monster?" "$a" && answer "umber of trials" "$b" &&
        answer "urns per trial" "$c" && answer "Player (c)hases" "$d" &&
        answer "whole group" "$f" && answer "andom seed" "$e" &&
        answer "Profile:" "$g" || exit 2
        label="kite $a ($d, profile $g)"
    fi
    wait_for "Continue?" 10 || exit 2
    tmux send-keys -t $SESS y
    wait_for "RNG fingerprint" "$TIMEOUT" || { echo "FAIL  $label: no result"; exit 1; }
    got=$(pane | sed -n 's/.*RNG fingerprint \([0-9A-F]\{8\}\).*/\1/p' | head -1)
    tmux send-keys -t $SESS Escape
    echo "$kind|$a|$b|$c|$d|$e|$f|$g|$h|$got" >> "$NEW"
    if [ "$got" = "$want" ]; then
        pass=$((pass + 1)); echo "ok    $label"
    else
        fail=$((fail + 1)); echo "FAIL  $label: fingerprint $got, expected ${want:-none}"
    fi
done < "$EXPECTED"

if [ $UPDATE = 1 ]; then
    cp "$NEW" "$EXPECTED"
    echo "updated $EXPECTED"
    exit 0
fi
echo "$pass passed, $fail failed"
[ $fail = 0 ]
