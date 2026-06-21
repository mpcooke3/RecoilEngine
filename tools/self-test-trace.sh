#!/bin/zsh
# Autonomous self-test: launches spring directly into a BAR vs BARbAI skirmish
# on Ravaged Remake (the map where we hit the burn-mark bug), captures
# screenshots via macOS `screencapture`, dumps every Vulkan command to a trace
# file, and shuts down cleanly so logs flush.
#
# Usage: tools/self-test-trace.sh [GAME_SECONDS]
#   GAME_SECONDS defaults to 60 — total time spring runs (including ~25s of
#   loading + ~20s for the commander beam-down scorch lifecycle).
#
# Output: build/vk-trace.log, build/mesa-debug.log, build/infolog.txt,
#         /tmp/self-test-shot-*.png

set -e

BAR_DIR="/Users/matthewcooke/WebstormProjects/BAR"
SPRING="$BAR_DIR/build/spring"
WRITE_PATH="$BAR_DIR/build"
SCRIPT="$BAR_DIR/tools/skirmish-trace.txt"
VK_SDK="$HOME/VulkanSDK/1.4.350.1/macOS"
DURATION="${1:-60}"

# Make sure the script targets the actually-installed BAR game version.
GAMETYPE=$(grep -oE 'arName="Beyond All Reason [^"]+"' "$WRITE_PATH/infolog.txt" 2>/dev/null \
    | head -1 | sed -E 's/^arName="//; s/"$//')
if [ -n "$GAMETYPE" ]; then
    sed -i.bak -E "s|gametype=Beyond All Reason [^;]+;|gametype=$GAMETYPE;|" "$SCRIPT"
    echo "[self-test] gametype set to: $GAMETYPE"
fi

# Kill any stale spring/launcher first
if pgrep -f "$SPRING" >/dev/null 2>&1; then
    echo "[self-test] killing stale spring"
    pkill -9 -f "$SPRING" 2>/dev/null || true
fi
if pgrep -lf "/electron/dist/Electron" | grep -q launcher; then
    echo "[self-test] killing stale launcher"
    pkill -9 -f "node_modules/electron/dist/Electron" 2>/dev/null || true
fi
sleep 1

# Mesa/Zink/KK env (matches launcher/src/config.json so spring can init GL)
export LIBGL_DRIVERS_PATH="$HOME/mesa-native/lib/dri"
export VK_DRIVER_FILES="$HOME/mesa-native/share/vulkan/icd.d/kosmickrisp_mesa_icd.aarch64.json"
export EGL_PLATFORM="surfaceless"
export MESA_LOADER_DRIVER_OVERRIDE="zink"
export MESA_GL_VERSION_OVERRIDE="4.6"
export MESA_GLSL_VERSION_OVERRIDE="460"

# Vulkan API dump setup
export VK_LAYER_PATH="$VK_SDK/share/vulkan/explicit_layer.d"
export DYLD_LIBRARY_PATH="$VK_SDK/lib:${DYLD_LIBRARY_PATH:-}"
export VK_LOADER_LAYERS_ENABLE="*api_dump*"
export VK_INSTANCE_LAYERS="VK_LAYER_LUNARG_api_dump"
export VK_LOADER_DEBUG="layer,error,warn"
export VK_ICD_FILENAMES="$HOME/mesa-native/share/vulkan/icd.d/kosmickrisp_mesa_icd.aarch64.json"
export VK_APIDUMP_LOG_FILENAME="$WRITE_PATH/vk-trace.log"
export VK_APIDUMP_OUTPUT_FORMAT=text
export VK_APIDUMP_FILE=true
export VK_APIDUMP_FLUSH=true
export VK_APIDUMP_TIMESTAMP=true
# Modern layer settings (VkLayerSettings) — both env var prefixes
export VK_LUNARG_API_DUMP_OUTPUT_FORMAT=text
export VK_LUNARG_API_DUMP_LOG_FILENAME="$WRITE_PATH/vk-trace.log"
export VK_LUNARG_API_DUMP_FILE=true
export VK_LAYER_SETTINGS_PATH="$WRITE_PATH/vk_layer_settings.txt"
: > "$VK_APIDUMP_LOG_FILENAME"

# Mesa/Zink debug
export MESA_DEBUG=context,silent
export ZINK_DEBUG=info,validation,quietfail,perf
export MESA_GLSL=cache_info,errors
export MESA_LOG_FILE="$WRITE_PATH/mesa-debug.log"
: > "$MESA_LOG_FILE"

# Spring's own log sections
export SPRING_LOG_SECTIONS="Shader,CSMFGroundTextures"

echo "[self-test] vk-trace → $VK_APIDUMP_LOG_FILENAME"
echo "[self-test] mesa-debug → $MESA_LOG_FILE"
echo "[self-test] launching spring (window mode, no fullscreen)"

# Run spring in the background. --write-dir tells it where to put infolog etc.
"$SPRING" --write-dir "$WRITE_PATH" "$SCRIPT" > /tmp/self-test-spring-stdout.log 2>&1 &
SPRING_PID=$!
echo "[self-test] spring PID: $SPRING_PID"

# Wait for game to actually start (look for GameStart in infolog)
echo "[self-test] waiting for game start (up to 60s)..."
WAITED=0
until grep -q "GameStart\|game start" "$WRITE_PATH/infolog.txt" 2>/dev/null; do
    sleep 1
    WAITED=$((WAITED + 1))
    if [ $WAITED -gt 60 ]; then
        echo "[self-test] timeout waiting for game start"
        break
    fi
    if ! kill -0 $SPRING_PID 2>/dev/null; then
        echo "[self-test] spring exited before game start"
        break
    fi
done
echo "[self-test] game appears to have started at t=${WAITED}s"

# Screenshots are now taken by the Auto-Screenshot widget inside the game.
# It also issues /quit when done. We just wait for spring to exit naturally.
echo "[self-test] in-game widget takes screenshots + quits; waiting for spring exit"

# Wait for spring to self-exit (widget calls /quit after the last shot)
WAITED=0
while kill -0 $SPRING_PID 2>/dev/null; do
    sleep 1
    WAITED=$((WAITED + 1))
    if [ $WAITED -gt $DURATION ]; then
        echo "[self-test] timeout — sending SIGTERM"
        kill -TERM $SPRING_PID 2>/dev/null || true
        sleep 3
        kill -9 $SPRING_PID 2>/dev/null || true
        break
    fi
done
echo "[self-test] spring exited after ${WAITED}s"

# Report results
echo "[self-test] -------- results --------"
ls -lh "$VK_APIDUMP_LOG_FILENAME" "$MESA_LOG_FILE" "$WRITE_PATH/infolog.txt" 2>/dev/null
ls -lh /tmp/self-test-shot-*.png 2>/dev/null
echo "[self-test] done"
