# shellcheck shell=bash
# Sourced by the scripts that run Xenon's tests in a container (Meta/ci/build-and-test.sh, Meta/ci/test-appimage.sh).
#
# The tests open Xenon's windows too (the session restore check: a headless run never saves its session), so they get a
# real X server, as on a desktop, rather than Qt's offscreen platform. Xvfb picks a free display and reports it through
# -displayfd once it's ready. This exports DISPLAY, and stops Xvfb when the script exits.

x_server_log=$(mktemp)
display_file=$(mktemp)
Xvfb -displayfd 3 -screen 0 1920x1080x24 -nolisten tcp 3>"$display_file" >"$x_server_log" 2>&1 &
x_server_pid=$!
trap 'kill "$x_server_pid" 2>/dev/null || true' EXIT
for _ in $(seq 1 300); do
  if [[ -s $display_file ]] || ! kill -0 "$x_server_pid" 2>/dev/null; then
    break
  fi
  sleep 0.1
done
if [[ ! -s $display_file ]]; then
  cat "$x_server_log" >&2
  echo "error: Xvfb didn't start" >&2
  exit 1
fi
DISPLAY=":$(tr -d '[:space:]' < "$display_file")"
export DISPLAY QT_QPA_PLATFORM=xcb
echo "display: $DISPLAY (Xvfb)"
