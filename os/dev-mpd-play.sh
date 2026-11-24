#!/bin/sh
TRACK=$(mpc listall | head -1)
echo "track: $TRACK"
mpc clear
mpc add "$TRACK"
mpc play
sleep 2
mpc status
