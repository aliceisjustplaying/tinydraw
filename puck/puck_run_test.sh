#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$root/scripts/puck-run"

puck_page='<!doctype html><title>device emulator</title><b id="deviceName"></b><canvas id="panel"></canvas>'
non_puck_page='<!doctype html><title>another service</title><main>hello</main>'
partial_page='<!doctype html><title>device emulator</title><b id="deviceName"></b>'
quit_ack='{"ok":true,"stopping":true}'

is_puck_page "$puck_page"
if is_puck_page "$non_puck_page" || is_puck_page "$partial_page"; then
  echo "Puck page marker accepted a non-Puck response" >&2
  exit 1
fi
is_quit_ack "$quit_ack"
if is_quit_ack '{"ok":true}' || is_quit_ack '<html>not an API response</html>'; then
  echo "Puck quit acknowledgement accepted an invalid response" >&2
  exit 1
fi

fixture="$(mktemp -d)"
cleanup_fixture() {
  rm -f "$fixture/package.json" "$fixture/server.ts"
  rmdir "$fixture/wasm" 2>/dev/null || true
  rmdir "$fixture" 2>/dev/null || true
}
trap cleanup_fixture EXIT
if is_puck_repo "$fixture"; then
  echo "empty directory was accepted as a Puck checkout" >&2
  exit 1
fi
touch "$fixture/package.json" "$fixture/server.ts"
mkdir "$fixture/wasm"
is_puck_repo "$fixture"

echo "puck-run probe tests passed"
