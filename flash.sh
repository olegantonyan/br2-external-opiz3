#!/bin/bash
set -euo pipefail

usage() {
	cat <<EOF
Usage: $0 [-d DIR] [-n] HOST [all|system|u-boot]

Writes NOR partitions of a running board over SSH, verifies them, reboots.
Partitions that already match are skipped. env and nvm are never touched.

  HOST    board address; root@ is added if no user is given
  -d DIR  images directory (default: output/images next to this script)
  -n      do not reboot

Extra ssh options go in SSH_OPTS, e.g. SSH_OPTS="-o StrictHostKeyChecking=no".
EOF
	exit 1
}

DIR="$(dirname "$(readlink -f "$0")")/output/images"
REBOOT=1
while getopts d:n o; do
	case $o in
		d) DIR=$OPTARG ;;
		n) REBOOT=0 ;;
		*) usage ;;
	esac
done
shift $((OPTIND - 1))
[ $# -ge 1 ] || usage
HOST=$1
[[ $HOST == *@* ]] || HOST=root@$HOST
case ${2:-all} in
	all) PARTS=(u-boot system) ;;
	u-boot | system) PARTS=("$2") ;;
	*) usage ;;
esac
declare -A FILE=([u-boot]=u-boot-sunxi-with-spl.bin [system]=nor-system.itb)

read -r -a OPTS <<< "${SSH_OPTS:-}"
remote() { ssh "${OPTS[@]}" "$HOST" "$@"; }

changed=0
for p in "${PARTS[@]}"; do
	f=$DIR/${FILE[$p]}
	[ -f "$f" ] || { echo "missing $f" >&2; exit 1; }
	size=$(stat -c %s "$f")
	sum=$(md5sum < "$f" | cut -d' ' -f1)
	dev=$(remote -n "awk -F: '/\"$p\"/ {print \$1}' /proc/mtd")
	[ -n "$dev" ] || { echo "$HOST: no \"$p\" MTD partition (old layout?)" >&2; exit 1; }
	max=$(remote -n "cat /sys/class/mtd/$dev/size")
	[ "$size" -le "$max" ] || { echo "$p: $size bytes do not fit /dev/$dev ($max)" >&2; exit 1; }
	if [ "$(remote -n "head -c $size /dev/$dev | md5sum" | cut -d' ' -f1)" = "$sum" ]; then
		echo "$p: up to date"
		continue
	fi
	echo "$p: writing $size bytes to /dev/$dev"
	got=$(remote "cat > /tmp/$p.bin && flashcp /tmp/$p.bin /dev/$dev; rm -f /tmp/$p.bin; head -c $size /dev/$dev | md5sum" < "$f" | cut -d' ' -f1)
	[ "$got" = "$sum" ] || { echo "$p: verify FAILED (flash $got, file $sum). Do not reboot; rerun." >&2; exit 1; }
	echo "$p: verified"
	changed=1
done

if [ $changed = 1 ] && [ $REBOOT = 1 ]; then
	echo "rebooting $HOST"
	remote -n reboot
fi
