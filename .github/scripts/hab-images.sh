#!/bin/sh
# SPDX-License-Identifier: GPL-2.0+
#
# Builds the boards booted by the virtual i.MX boot ROMs with
# CONFIG_IMX_HAB and signs their boot images with a PKI created by
# hab-pki.sh. Requires cst and the cross compilers in PATH, and TFA_TAG
# for the i.MX 8M boards.
#
# usage: hab-images.sh <pki dir> <output dir>

set -e

pki=$(realpath "$1")
out=$(realpath -m "$2")
scripts=$(dirname "$(realpath "$0")")
suffix=sha256_2048_65537_v3
mkdir -p "$out"

# <defconfig> <cross compiler prefix>
build() {
	b=build-hab-$1
	make -s O=$b CROSS_COMPILE=$2 $1_defconfig
	./scripts/config --file $b/.config -e IMX_HAB
	make -s O=$b CROSS_COMPILE=$2 olddefconfig
	make -s O=$b CROSS_COMPILE=$2 -j$(nproc)
}

export SOURCE_DATE_EPOCH=$(git log -1 --format=%ct)

for board in mx6qsabrelite mx7dsabresd; do
	build $board arm-none-eabi-
	"$scripts/hab-sign.sh" "$pki" build-hab-$board/u-boot-dtb.imx \
		"$out/$board-hab-u-boot-dtb.imx"
done

# SPL is authenticated by the ROM, U-Boot proper by SPL via the ROM
build mx6ul_14x14_evk arm-none-eabi-
for img in SPL u-boot-ivt.img; do
	"$scripts/hab-sign.sh" "$pki" build-hab-mx6ul_14x14_evk/$img \
		"$out/mx6ul_14x14_evk-hab-$img"
done

# binman signs flash.bin with CST, see tools/binman/etype/nxp_imx8mcst.py
export SRK_TABLE=$pki/crts/SRK_1_2_3_4_table.bin
export CSF_KEY=$pki/crts/CSF1_1_${suffix}_usr_crt.pem
export IMG_KEY=$pki/crts/IMG1_1_${suffix}_usr_crt.pem
for soc in imx8mm imx8mp; do
	"$scripts/imx8m-blobs.sh" $soc build-hab-${soc}_evk
	build ${soc}_evk aarch64-linux-gnu-
	# binman merely warns about a missing cst, so check for both CSFs
	n=0
	for csf in build-hab-${soc}_evk/nxp.csf-output-blob.*; do
		[ -s "$csf" ] && n=$((n + 1))
	done
	[ $n -eq 2 ] || { echo "${soc}_evk: flash.bin not signed" >&2; exit 1; }
	cp build-hab-${soc}_evk/flash.bin "$out/${soc}_evk-hab-flash.bin"
done

cp "$pki/crts/SRK_1_2_3_4_fuse.bin" "$out/hab-SRK_1_2_3_4_fuse.bin"
