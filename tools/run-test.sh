#!/bin/zsh
# Unified test runner for the autonomous mac BAR test harness.
#
# Usage: tools/run-test.sh <test> [duration_seconds]
#
#   <test> ∈ { scorch, trees, explosions }
#     scorch     — commander beam-down + scorch CEG (the black-square repro)
#     trees      — flies camera to first tree feature, screenshots
#     explosions — captures the first explosion (natural or cheat-spawned)
#
#   duration_seconds — hard wall-clock timeout. Default: 60.
#
# Each test enables exactly one of the dbg_test_* widgets in BYAR.lua,
# leaves the others off, and runs spring straight into a skirmish.

set -e

TEST="${1:?usage: run-test.sh <scorch|trees|explosions> [duration]}"
DURATION="${2:-60}"

BAR_DIR="/Users/matthewcooke/WebstormProjects/BAR"
WIDGETS_DIR="$BAR_DIR/build/LuaUI/Widgets"
SRC_WIDGETS="$BAR_DIR/tools/widgets"
CONFIG="$BAR_DIR/build/LuaUI/Config/BYAR.lua"

# Sync tracked widget sources into the writepath so spring sees them.
mkdir -p "$WIDGETS_DIR"
for w in "$SRC_WIDGETS"/*.lua; do
    [ -f "$w" ] && cp -f "$w" "$WIDGETS_DIR/"
done

# Sync tracked BAR-content overrides (CUS shader patch, etc.) into the writepath.
# VFS.LoadFile is RAW_FIRST in widget/gadget context, so files in the writepath
# take precedence over BAR's archive copies.
if [ -d "$BAR_DIR/tools/content" ]; then
    (cd "$BAR_DIR/tools/content" && find . -type f | while read -r f; do
        dest="$BAR_DIR/build/${f#./}"
        mkdir -p "$(dirname "$dest")"
        cp -f "$BAR_DIR/tools/content/${f#./}" "$dest"
    done)
fi

case "$TEST" in
    scorch)
        WIDGET_NAME="Auto Screenshot (mac debug)"
        DESC="commander beam-down scorch CEG"
        ;;
    trees)
        WIDGET_NAME="DBG Test Trees"
        DESC="live + dead tree features"
        ;;
    explosions)
        WIDGET_NAME="DBG Test Explosions"
        DESC="first explosion (natural or forced via cheat)"
        ;;
    selection)
        WIDGET_NAME="DBG Test Selection"
        DESC="unit selection indicator (unselected / all-selected / one-selected)"
        ;;
    *)
        echo "Unknown test '$TEST' — choose scorch | trees | explosions | selection"
        exit 2
        ;;
esac

echo "[run-test] selected: $TEST ($DESC)"
echo "[run-test] enabling widget: $WIDGET_NAME"

# BAR auto-adds new widgets to BYAR.lua at end-of-run with order=0. We just
# sed-update entries that already exist. If the test widget hasn't been
# discovered yet (first ever run), the test will be a no-op; rerun once.
have_entry() { grep -q "\[\"$1\"\] = " "$CONFIG"; }
if ! have_entry "$WIDGET_NAME"; then
    echo "[run-test] '$WIDGET_NAME' not yet known to BAR's config — running once to register it"
    echo "[run-test] this run will be a no-op; rerun the same command afterwards"
fi

# Disable all dbg_test_* widgets, then enable the chosen one.
for w in "Auto Screenshot (mac debug)" "DBG Test Trees" "DBG Test Explosions" "DBG Test Selection"; do
    sed -i.bak "s/\[\"$w\"\] = [0-9]*,/[\"$w\"] = 0,/" "$CONFIG"
done
sed -i.bak "s/\[\"$WIDGET_NAME\"\] = 0,/[\"$WIDGET_NAME\"] = 1,/" "$CONFIG"
rm -f "$CONFIG.bak"

# Clear old screenshots for this run
mkdir -p "$BAR_DIR/build/screenshots"
find "$BAR_DIR/build/screenshots" -name "*dbg-${TEST}*.png" -delete 2>/dev/null || true

# Always disable all DBG Test widgets when this script exits so that a
# subsequent normal-launch (via the Electron launcher) doesn't accidentally
# fire one of them. BAR rewrites BYAR.lua at end-of-run with whatever order
# values are in effect at the time, so we re-disable after the run, then
# again on script exit just in case.
disable_all_dbg_widgets() {
    [ -f "$CONFIG" ] || return 0
    for w in "Auto Screenshot (mac debug)" "DBG Test Trees" "DBG Test Explosions" "DBG Test Selection"; do
        sed -i.bak "s/\[\"$w\"\] = [0-9]*,/[\"$w\"] = 0,/" "$CONFIG"
    done
    rm -f "$CONFIG.bak"
}
trap disable_all_dbg_widgets EXIT

# Delegate to the underlying spring-launch script
"$BAR_DIR/tools/self-test-trace.sh" "$DURATION"

# Explicitly disable after the run (the trap will also fire on script exit)
disable_all_dbg_widgets

# Report what we got
echo "[run-test] --------- $TEST shots ---------"
ls -1t "$BAR_DIR/build/screenshots" 2>/dev/null | head -10
