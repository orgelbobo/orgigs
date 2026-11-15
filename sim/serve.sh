#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Serve the simulator on http://localhost:8099 (Web MIDI wants a secure context: localhost is one).
cd "$(dirname "$0")/web" && exec python3 -m http.server "${PORT:-8099}" --bind 127.0.0.1
