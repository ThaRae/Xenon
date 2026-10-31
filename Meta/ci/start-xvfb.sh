# shellcheck shell=bash

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
