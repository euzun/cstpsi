#!/bin/bash
# CSTPSI -- Composable Set-Threshold Labeled PSI
# Author: Erkam Uzun
# Copyright (c) 2026 Erkam Uzun. PolyForm Noncommercial License 1.0.0.
#
# Split-screen network demo for CSTPSI
# Automatically opens sender and receiver in side-by-side terminals

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Detect terminal multiplexer
if command -v tmux &> /dev/null; then
    # Use tmux
    echo "Using tmux for split-screen demo..."

    SESSION="cstpsi_demo"
    tmux new-session -d -s "$SESSION" -x 200 -y 50
    tmux send-keys -t "$SESSION" "cd '$SCRIPT_DIR' && ./run_sender.sh" C-m
    tmux split-window -h -t "$SESSION"
    tmux send-keys -t "$SESSION" "sleep 5 && cd '$SCRIPT_DIR' && ./run_receiver.sh" C-m
    tmux attach-session -t "$SESSION"

elif [[ "$OSTYPE" == "darwin"* ]]; then
    # Use iTerm2 on macOS if available
    if [ -d "/Applications/iTerm.app" ]; then
        echo "Using iTerm2 for split-screen demo..."
        osascript <<EOF
tell application "iTerm"
    create window with default profile
    tell current session of current window
        split horizontally with default profile
        write text "cd '$SCRIPT_DIR' && ./run_sender.sh"
    end tell
    tell second session of current tab of current window
        write text "cd '$SCRIPT_DIR' && sleep 5 && ./run_receiver.sh"
    end tell
end tell
EOF
    else
        echo "Error: Neither tmux nor iTerm2 found."
        echo "Please install tmux: brew install tmux"
        exit 1
    fi
else
    echo "Error: tmux not found and not on macOS."
    echo "Please install tmux: apt-get install tmux (Ubuntu) or brew install tmux (macOS)"
    exit 1
fi
