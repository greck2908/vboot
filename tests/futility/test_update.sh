#!/bin/bash -eux
# Copyright 2018 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

me=${0##*/}
TMP="$me.tmp"

# Test --sys_props (primitive test needed for future updating tests).
test_sys_props() {
	! "${FUTILITY}" --debug update --sys_props "$*" 2>&1 |
		sed -n 's/.*property\[\(.*\)].value = \(.*\)/\1,\2,/p' |
		tr '\n' ' '
}

test "$(test_sys_props "1,2,3")" = "0,1, 1,2, 2,3, "
test "$(test_sys_props "1 2 3")" = "0,1, 1,2, 2,3, "
test "$(test_sys_props "1, 2,3 ")" = "0,1, 1,2, 2,3, "
test "$(test_sys_props "   1,, 2")" = "0,1, 2,2, "
test "$(test_sys_props " , 4,")" = "1,4, "

test_quirks() {
	! "${FUTILITY}" --debug update --quirks "$*" 2>&1 |
		sed -n 's/.*Set quirk \(.*\) to \(.*\)./\1,\2/p' |
		tr '\n' ' '
}

test "$(test_quirks "enlarge_image")" = "enlarge_image,1 "
test "$(test_quirks "enlarge_image=2")" = "enlarge_image,2 "
test "$(test_quirks " enlarge_image, enlarge_image=2")" = \
	"enlarge_image,1 enlarge_image,2 "

# Test data files
LINK_BIOS="${SCRIPTDIR}/data/bios_link_mp.bin"
PEPPY_BIOS="${SCRIPTDIR}/data/bios_peppy_mp.bin"

# Work in scratch directory
cd "$OUTDIR"
set -o pipefail

# In all the test scenario, we want to test "updating from PEPPY to LINK".
TO_IMAGE=${TMP}.src.link
FROM_IMAGE=${TMP}.src.peppy
TO_HWID="X86 LINK TEST 6638"
FROM_HWID="X86 PEPPY TEST 4211"
cp -f ${LINK_BIOS} ${TO_IMAGE}
cp -f ${PEPPY_BIOS} ${FROM_IMAGE}

patch_file() {
	local file="$1"
	local section="$2"
	local section_offset="$3"
	local data="$4"

	# NAME OFFSET SIZE
	local fmap_info="$(${FUTILITY} dump_fmap -p ${file} ${section})"
	local base="$(echo "${fmap_info}" | sed 's/^[^ ]* //; s/ [^ ]*$//')"
	local offset=$((base + section_offset))
	echo "offset: ${offset}"
	printf "${data}" | dd of="${file}" bs=1 seek="${offset}" conv=notrunc
}

# PEPPY and LINK have different platform element ("Google_Link" and
# "Google_Peppy") in firmware ID so we want to hack them by changing
# "Google_" to "Google.".
patch_file ${TO_IMAGE} RW_FWID_A 0 Google.
patch_file ${TO_IMAGE} RW_FWID_B 0 Google.
patch_file ${TO_IMAGE} RO_FRID 0 Google.
patch_file ${FROM_IMAGE} RW_FWID_A 0 Google.
patch_file ${FROM_IMAGE} RW_FWID_B 0 Google.
patch_file ${FROM_IMAGE} RO_FRID 0 Google.

unpack_image() {
	local folder="${TMP}.$1"
	local image="$2"
	mkdir -p "${folder}"
	(cd "${folder}" && ${FUTILITY} dump_fmap -x "../${image}")
	${FUTILITY} gbb -g --rootkey="${folder}/rootkey" "${image}"
}

# Unpack images so we can prepare expected results by individual sections.
unpack_image "to" "${TO_IMAGE}"
unpack_image "from" "${FROM_IMAGE}"

# Hack FROM_IMAGE so it has same root key as TO_IMAGE (for RW update).
FROM_DIFFERENT_ROOTKEY_IMAGE="${FROM_IMAGE}2"
cp -f "${FROM_IMAGE}" "${FROM_DIFFERENT_ROOTKEY_IMAGE}"
"${FUTILITY}" gbb -s --rootkey="${TMP}.to/rootkey" "${FROM_IMAGE}"

# Hack for quirks
cp -f "${FROM_IMAGE}" "${FROM_IMAGE}.large"
truncate -s $((8388608 * 2)) "${FROM_IMAGE}.large"

# Generate expected results.
cp -f "${TO_IMAGE}" "${TMP}.expected.full"
cp -f "${FROM_IMAGE}" "${TMP}.expected.rw"
cp -f "${FROM_IMAGE}" "${TMP}.expected.a"
cp -f "${FROM_IMAGE}" "${TMP}.expected.b"
cp -f "${FROM_IMAGE}" "${TMP}.expected.legacy"
"${FUTILITY}" gbb -s --hwid="${FROM_HWID}" "${TMP}.expected.full"
"${FUTILITY}" load_fmap "${TMP}.expected.full" \
	RW_VPD:${TMP}.from/RW_VPD \
	RO_VPD:${TMP}.from/RO_VPD
"${FUTILITY}" load_fmap "${TMP}.expected.rw" \
	RW_SECTION_A:${TMP}.to/RW_SECTION_A \
	RW_SECTION_B:${TMP}.to/RW_SECTION_B \
	RW_SHARED:${TMP}.to/RW_SHARED \
	RW_LEGACY:${TMP}.to/RW_LEGACY
"${FUTILITY}" load_fmap "${TMP}.expected.a" \
	RW_SECTION_A:${TMP}.to/RW_SECTION_A
"${FUTILITY}" load_fmap "${TMP}.expected.b" \
	RW_SECTION_B:${TMP}.to/RW_SECTION_B
"${FUTILITY}" load_fmap "${TMP}.expected.legacy" \
	RW_LEGACY:${TMP}.to/RW_LEGACY
cp -f "${TMP}.expected.full" "${TMP}.expected.large"
dd if=/dev/zero bs=8388608 count=1 | tr '\000' '\377' >>"${TMP}.expected.large"
cp -f "${TMP}.expected.full" "${TMP}.expected.me_unlocked"
patch_file "${TMP}.expected.me_unlocked" SI_DESC 128 \
	"\x00\xff\xff\xff\x00\xff\xff\xff\x00\xff\xff\xff"

test_update() {
	local test_name="$1"
	local emu_src="$2"
	local expected="$3"
	local error_msg="${expected#!}"
	local msg

	shift 3
	cp -f "${emu_src}" "${TMP}.emu"
	echo "*** Test Item: ${test_name}"
	if [ "${error_msg}" != "${expected}" ] && [ -n "${error_msg}" ]; then
		msg="$(! "${FUTILITY}" update --emulate "${TMP}.emu" "$@" 2>&1)"
		echo "${msg}" | grep -qF -- "${error_msg}"
	else
		"${FUTILITY}" update --emulate "${TMP}.emu" "$@"
		cmp "${TMP}.emu" "${expected}"
	fi
}

# --sys_props: mainfw_act, tpm_fwver, is_vboot2, platform_ver, [wp_hw, wp_sw]
# tpm_fwver = <data key version:16><firmware version:16>.
# TO_IMAGE is signed with data key version = 1, firmware version = 4 => 0x10004.

# Test Full update.
test_update "Full update" \
	"${FROM_IMAGE}" "${TMP}.expected.full" \
	-i "${TO_IMAGE}" --wp=0 --sys_props 0,0x10001,1

test_update "Full update (incompatible platform)" \
	"${FROM_IMAGE}" "!platform is not compatible" \
	-i "${LINK_BIOS}" --wp=0 --sys_props 0,0x10001,1

test_update "Full update (TPM Anti-rollback: data key)" \
	"${FROM_IMAGE}" "!Data key version rollback detected (2->1)" \
	-i "${TO_IMAGE}" --wp=0 --sys_props 1,0x20001,1

test_update "Full update (TPM Anti-rollback: kernel key)" \
	"${FROM_IMAGE}" "!Firmware version rollback detected (5->4)" \
	-i "${TO_IMAGE}" --wp=0 --sys_props 1,0x10005,1

test_update "Full update (TPM Anti-rollback: 0 as tpm_fwver)" \
	"${FROM_IMAGE}" "${TMP}.expected.full" \
	-i "${TO_IMAGE}" --wp=0 --sys_props 0,0x0,1

test_update "Full update (TPM check failure due to invalid tpm_fwver)" \
	"${FROM_IMAGE}" "!Invalid tpm_fwver: -1" \
	-i "${TO_IMAGE}" --wp=0 --sys_props 0,-1,1

test_update "Full update (Skip TPM check with --force)" \
	"${FROM_IMAGE}" "${TMP}.expected.full" \
	-i "${TO_IMAGE}" --wp=0 --sys_props 0,-1,1 --force

test_update "Full update (from stdin)" \
	"${FROM_IMAGE}" "${TMP}.expected.full" \
	-i - --wp=0 --sys_props 0,-1,1 --force <"${TO_IMAGE}"

# Test RW-only update.
test_update "RW update" \
	"${FROM_IMAGE}" "${TMP}.expected.rw" \
	-i "${TO_IMAGE}" --wp=1 --sys_props 0,0x10001,1

test_update "RW update (incompatible platform)" \
	"${FROM_IMAGE}" "!platform is not compatible" \
	-i "${LINK_BIOS}" --wp=1 --sys_props 0,0x10001,1

test_update "RW update (incompatible rootkey)" \
	"${FROM_DIFFERENT_ROOTKEY_IMAGE}" "!RW not signed by same RO root key" \
	-i "${TO_IMAGE}" --wp=1 --sys_props 0,0x10001,1

test_update "RW update (TPM Anti-rollback: data key)" \
	"${FROM_IMAGE}" "!Data key version rollback detected (2->1)" \
	-i "${TO_IMAGE}" --wp=1 --sys_props 1,0x20001,1

test_update "RW update (TPM Anti-rollback: kernel key)" \
	"${FROM_IMAGE}" "!Firmware version rollback detected (5->4)" \
	-i "${TO_IMAGE}" --wp=1 --sys_props 1,0x10005,1

# Test Try-RW update (vboot2).
test_update "RW update (A->B)" \
	"${FROM_IMAGE}" "${TMP}.expected.b" \
	-i "${TO_IMAGE}" -t --wp=1 --sys_props 0,0x10001,1

test_update "RW update (B->A)" \
	"${FROM_IMAGE}" "${TMP}.expected.a" \
	-i "${TO_IMAGE}" -t --wp=1 --sys_props 1,0x10001,1

test_update "RW update -> fallback to RO+RW Full update" \
	"${FROM_IMAGE}" "${TMP}.expected.full" \
	-i "${TO_IMAGE}" -t --wp=0 --sys_props 1,0x10002,1
test_update "RW update (incompatible platform)" \
	"${FROM_IMAGE}" "!platform is not compatible" \
	-i "${LINK_BIOS}" -t --wp=1 --sys_props 0x10001,1

test_update "RW update (incompatible rootkey)" \
	"${FROM_DIFFERENT_ROOTKEY_IMAGE}" "!RW not signed by same RO root key" \
	-i "${TO_IMAGE}" -t --wp=1 --sys_props 0,0x10001,1

test_update "RW update (TPM Anti-rollback: data key)" \
	"${FROM_IMAGE}" "!Data key version rollback detected (2->1)" \
	-i "${TO_IMAGE}" -t --wp=1 --sys_props 1,0x20001,1

test_update "RW update (TPM Anti-rollback: kernel key)" \
	"${FROM_IMAGE}" "!Firmware version rollback detected (5->4)" \
	-i "${TO_IMAGE}" -t --wp=1 --sys_props 1,0x10005,1

test_update "RW update -> fallback to RO+RW Full update (TPM Anti-rollback)" \
	"${TO_IMAGE}" "!Firmware version rollback detected (4->2)" \
	-i "${FROM_IMAGE}" -t --wp=0 --sys_props 1,0x10004,1

# Test Try-RW update (vboot1).
test_update "RW update (vboot1, A->B)" \
	"${FROM_IMAGE}" "${TMP}.expected.b" \
	-i "${TO_IMAGE}" -t --wp=1 --sys_props 0,0 --sys_props 0,0x10001,0

test_update "RW update (vboot1, B->B)" \
	"${FROM_IMAGE}" "${TMP}.expected.b" \
	-i "${TO_IMAGE}" -t --wp=1 --sys_props 1,0 --sys_props 0,0x10001,0

# Test 'factory mode'
test_update "Factory mode update (WP=0)" \
	"${FROM_IMAGE}" "${TMP}.expected.full" \
	-i "${TO_IMAGE}" --wp=0 --sys_props 0,0x10001,1 --mode=factory

test_update "Factory mode update (WP=0)" \
	"${FROM_IMAGE}" "${TMP}.expected.full" \
	--factory -i "${TO_IMAGE}" --wp=0 --sys_props 0,0x10001,1

test_update "Factory mode update (WP=1)" \
	"${FROM_IMAGE}" "!needs WP disabled" \
	-i "${TO_IMAGE}" --wp=1 --sys_props 0,0x10001,1 --mode=factory

test_update "Factory mode update (WP=1)" \
	"${FROM_IMAGE}" "!needs WP disabled" \
	--factory -i "${TO_IMAGE}" --wp=1 --sys_props 0,0x10001,1

# Test legacy update
test_update "Legacy update" \
	"${FROM_IMAGE}" "${TMP}.expected.legacy" \
	-i "${TO_IMAGE}" --mode=legacy

# Test quirks
test_update "Full update (wrong size)" \
	"${FROM_IMAGE}.large" "!Image size is different" \
	-i "${TO_IMAGE}" --wp=0 --sys_props 0,0x10001,1

test_update "Full update (--quirks enlarge_image)" \
	"${FROM_IMAGE}.large" "${TMP}.expected.large" --quirks enlarge_image \
	-i "${TO_IMAGE}" --wp=0 --sys_props 0,0x10001,1

test_update "Full update (--quirks unlock_me_for_update)" \
	"${FROM_IMAGE}" "${TMP}.expected.me_unlocked" \
	--quirks unlock_me_for_update \
	-i "${TO_IMAGE}" --wp=0 --sys_props 0,0x10001,1

test_update "Full update (failure by --quirks min_platform_version)" \
	"${FROM_IMAGE}" "!Need platform version >= 3 (current is 2)" \
	--quirks min_platform_version=3 \
	-i "${TO_IMAGE}" --wp=0 --sys_props 0,0x10001,1,2

test_update "Full update (--quirks min_platform_version)" \
	"${FROM_IMAGE}" "${TMP}.expected.full" \
	--quirks min_platform_version=3 \
	-i "${TO_IMAGE}" --wp=0 --sys_props 0,0x10001,1,3

mkdir -p "${TMP}.archive"
cp -f "${LINK_BIOS}" "${TMP}.archive/bios.bin"
cp -f "${TO_IMAGE}" "${TMP}.archive/image_in_archive"
test_update "Full update (--archive)" \
	"${FROM_IMAGE}" "${TMP}.expected.full" \
	-a "${TMP}.archive" \
	-i "image_in_archive" --wp=0 --sys_props 0,0x10001,1,3
echo "TEST: Manifest (--manifest)"
${FUTILITY} update -a "${TMP}.archive" --manifest >"${TMP}.json.out"
cmp "${TMP}.json.out" "${SCRIPTDIR}/link.manifest.json"

# Test special programmer
if type flashrom >/dev/null 2>&1; then
	echo "TEST: Full update (dummy programmer)"
	cp -f "${FROM_IMAGE}" "${TMP}.emu"
	sudo "${FUTILITY}" update --programmer \
		dummy:emulate=VARIABLE_SIZE,image=${TMP}.emu,size=8388608 \
		-i "${TO_IMAGE}" --wp=0 --sys_props 0,0x10001,1,3 >&2
	cmp "${TMP}.emu" "${TMP}.expected.full"
fi

if type cbfstool >/dev/null 2>&1; then
	echo "SMM STORE" >"${TMP}.smm"
	truncate -s 262144 "${TMP}.smm"
	cp -f "${FROM_IMAGE}" "${TMP}.from.smm"
	cp -f "${TMP}.expected.full" "${TMP}.expected.full_smm"
	cbfstool "${TMP}.from.smm" add -r RW_LEGACY -n "smm store" \
		-f "${TMP}.smm" -t raw
	cbfstool "${TMP}.expected.full_smm" add -r RW_LEGACY -n "smm store" \
		-f "${TMP}.smm" -t raw -b 0x1bf000
	test_update "Legacy update (--quirks eve_smm_store)" \
		"${TMP}.from.smm" "${TMP}.expected.full_smm" \
		-i "${TO_IMAGE}" --wp=0 --sys_props 0,0x10001,1 \
		--quirks eve_smm_store
fi
