#!/bin/sh
# SPDX-License-Identifier: GPL-2.0+
#
# Signs an i.MX6/i.MX7 boot image (u-boot-dtb.imx, SPL or u-boot-ivt.img)
# with CST using a PKI created by hab-pki.sh. The blocks to authenticate are
# taken from the image's mkimage log, see doc/imx/habv4/.
#
# usage: hab-sign.sh <pki dir> <image> <signed image>

set -e

pki=$(realpath "$1")
img=$(realpath "$2")
out=$(realpath -m "$3")
suffix=sha256_2048_65537_v3

blocks=$(sed -n 's/^HAB Blocks: *//p' "$img.log")
[ -n "$blocks" ] || { echo "no HAB blocks in $img.log" >&2; exit 1; }

tmp=$(mktemp -d)
cat > "$tmp/csf.txt" <<EOF
[Header]
    Version = 4.2
    Hash Algorithm = sha256
    Engine Configuration = 0
    Certificate Format = X509
    Signature Format = CMS
    Engine = CAAM

[Install SRK]
    File = "$pki/crts/SRK_1_2_3_4_table.bin"
    Source index = 0

[Install CSFK]
    File = "$pki/crts/CSF1_1_${suffix}_usr_crt.pem"

[Authenticate CSF]

[Install Key]
    Verification index = 0
    Target Index = 2
    File = "$pki/crts/IMG1_1_${suffix}_usr_crt.pem"

[Authenticate Data]
    Verification index = 2
    Blocks = $blocks "$img"
EOF

(cd "$pki/keys" && cst -i "$tmp/csf.txt" -o "$tmp/csf.bin") >"$tmp/cst.log" 2>&1 ||
	{ cat "$tmp/cst.log" >&2; exit 1; }
cat "$img" "$tmp/csf.bin" > "$out"
rm -rf "$tmp"
