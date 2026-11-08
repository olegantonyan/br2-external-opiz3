#!/bin/bash
set -euo pipefail

usage() {
	cat <<EOF
Usage: $0 HOST [stress-ng args...]

Builds a static stress-ng with the Buildroot toolchain (once, into output/stress-ng/),
copies it to /tmp (RAM) on the board, runs it there, removes it afterwards.

  HOST  board address; root@ is added if no user is given

Example: $0 br2-external-opiz3.local --cpu 4 --vm 2 --vm-bytes 25% --verify -t 10m --metrics-brief
Extra ssh options go in SSH_OPTS, e.g. SSH_OPTS="-o StrictHostKeyChecking=no".
EOF
	exit 1
}

[ $# -ge 1 ] || usage
HOST=$1
shift
[[ $HOST == *@* ]] || HOST=root@$HOST

O="$(dirname "$(readlink -f "$0")")/output"
W=$O/stress-ng
BIN=$W/stress-ng

if [ ! -x "$BIN" ]; then
	make -C "$O" stress-ng-source
	eval "$(make -s -C "$O" printvars VARS='STRESS_NG_DL_DIR STRESS_NG_SOURCE' QUOTED_VARS=YES)"
	rm -rf "$W"
	mkdir -p "$W/src"
	tar -xf "$STRESS_NG_DL_DIR/$STRESS_NG_SOURCE" -C "$W/src" --strip-components=1
	make -C "$W/src" CC="$O/host/bin/aarch64-linux-gcc" STATIC=1 -j"$(nproc)"
	"$O/host/bin/aarch64-linux-strip" -o "$BIN" "$W/src/stress-ng"
fi

read -r -a OPTS <<< "${SSH_OPTS:-}"
ssh "${OPTS[@]}" "$HOST" 'cat > /tmp/stress-ng && chmod +x /tmp/stress-ng' < "$BIN"
trap 'ssh "${OPTS[@]}" -n "$HOST" "rm -f /tmp/stress-ng"' EXIT
CMD=/tmp/stress-ng
[ $# -eq 0 ] || CMD+=" $(printf '%q ' "$@")"
ssh "${OPTS[@]}" -t "$HOST" "$CMD"
