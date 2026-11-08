#!/bin/sh
set -eu

BIN="$1"
W="$BIN/nor"

# Must match patches/linux/0009 (DT partitions), uboot.fragment (bootcmd) and flash.sh.
UBOOT_OFF=0x000000 UBOOT_SIZE=0x0f0000
SYSTEM_OFF=0x100000 SYSTEM_SIZE=0xd00000
FLASH_SIZE=0x1000000

rm -rf "$W"
mkdir -p "$W"

xz --format=lzma -9 -c "$BIN/Image" > "$W/Image.lzma"

fdt_image() {
	cat <<EOF
		$1 {
			data = /incbin/("$2");
			type = "flat_dt";
			arch = "arm64";
			compression = "none";
			hash { algo = "crc32"; };
		};
EOF
}

{
	cat <<EOF
/dts-v1/;
/ {
	description = "Orange Pi Zero 3 NOR system";
	#address-cells = <1>;
	images {
		kernel {
			data = /incbin/("$W/Image.lzma");
			type = "kernel";
			arch = "arm64";
			os = "linux";
			compression = "lzma";
			load = <0x40200000>;
			entry = <0x40200000>;
			hash { algo = "crc32"; };
		};
		ramdisk {
			data = /incbin/("$BIN/rootfs.cpio.xz");
			type = "ramdisk";
			arch = "arm64";
			os = "linux";
			compression = "none";
			hash { algo = "crc32"; };
		};
EOF
	fdt_image fdt-base "$BIN/allwinner/sun50i-h618-orangepi-zero3.dtb"
	for f in "$BIN"/allwinner/opiz3-*.dtbo; do
		n=$(basename "$f" .dtbo)
		fdt_image "ov-${n#opiz3-}" "$f"
	done
	cat <<EOF
	};
	configurations {
		default = "conf-base";
		conf-base { kernel = "kernel"; ramdisk = "ramdisk"; fdt = "fdt-base"; };
EOF
	for f in "$BIN"/allwinner/opiz3-*.dtbo; do
		n=$(basename "$f" .dtbo)
		echo "		ov-${n#opiz3-} { fdt = \"ov-${n#opiz3-}\"; };"
	done
	echo "	};"
	echo "};"
} > "$W/system.its"

rm -f "$BIN/nor-system.itb"
mkimage -f "$W/system.its" "$BIN/nor-system.itb" > /dev/null
[ -s "$BIN/nor-system.itb" ] || { echo "ERROR: mkimage failed" >&2; exit 1; }

fit() {
	s=$(stat -c %s "$2")
	printf '%-8s %8d / %8d bytes  %s\n' "$1" "$s" $(($3)) "$(basename "$2")"
	[ "$s" -le $(($3)) ] || { echo "ERROR: $1 does not fit its partition" >&2; exit 1; }
}

fit u-boot "$BIN/u-boot-sunxi-with-spl.bin" $UBOOT_SIZE
fit system "$BIN/nor-system.itb" $SYSTEM_SIZE

put() {
	dd if="$2" of="$BIN/nor.img" bs=64k seek=$(($1 / 65536)) conv=notrunc status=none
}

head -c $((FLASH_SIZE)) /dev/zero | tr '\000' '\377' > "$BIN/nor.img"
put $UBOOT_OFF "$BIN/u-boot-sunxi-with-spl.bin"
put $SYSTEM_OFF "$BIN/nor-system.itb"
rm -f "$BIN/nor-kernel.itb"
echo "nor.img: $(stat -c %s "$BIN/nor.img") bytes"
