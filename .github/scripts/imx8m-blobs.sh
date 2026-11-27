#!/bin/sh
# SPDX-License-Identifier: GPL-2.0+
#
# Provides the binaries which binman includes in flash.bin of the i.MX 8M
# boards: bl31.bin of upstream TF-A, which is built in ./tfa, and empty
# placeholders of the original sizes for the DDR PHY training firmware.
# QEMU doesn't execute the latter, so NXP's firmware isn't redistributed.
#
# usage: imx8m-blobs.sh <imx8mm|imx8mp> <U-Boot build dir>

set -e

plat=$1
out=$2

if [ ! -d tfa ]; then
	git clone -q --depth 1 --branch "${TFA_TAG:?}" \
		https://github.com/ARM-software/arm-trusted-firmware tfa
	git -C tfa submodule update -q --init --depth 1 contrib/dtc
fi
make -s -C tfa CROSS_COMPILE=aarch64-linux-gnu- PLAT=$plat bl31 -j$(nproc)

mkdir -p "$out"
cp tfa/build/$plat/release/bl31.bin "$out/"
truncate -s 32364 "$out/lpddr4_pmu_train_1d_imem_202006.bin"
truncate -s 1660 "$out/lpddr4_pmu_train_1d_dmem_202006.bin"
truncate -s 25456 "$out/lpddr4_pmu_train_2d_imem_202006.bin"
truncate -s 1404 "$out/lpddr4_pmu_train_2d_dmem_202006.bin"
