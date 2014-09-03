#!/bin/bash -eux
# Copyright 2014 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

me=${0##*/}
TMP="$me.tmp"

# Work in scratch directory
cd "$OUTDIR"

KEYDIR=${SRCDIR}/tests/devkeys

# The input BIOS images are all signed with MP keys. We resign them with dev
# keys, which means we can precalculate the expected results. Note that the
# script does not change the root or recovery keys in the GBB.
INFILES="
${SCRIPTDIR}/data/bios_link_mp.bin
${SCRIPTDIR}/data/bios_mario_mp.bin
${SCRIPTDIR}/data/bios_peppy_mp.bin
${SCRIPTDIR}/data/bios_zgb_mp.bin
"

# We also want to test that we can sign an image without any valid firmware
# preambles. That one won't be able to tell how much of the FW_MAIN region is
# the valid firmware, so it'll have to sign the entire region.
GOOD_VBLOCKS=${SCRIPTDIR}/data/bios_peppy_mp.bin
ONEMORE=bios_peppy_mp_no_vblock.bin
cp ${GOOD_VBLOCKS} ${ONEMORE}
${FUTILITY} load_fmap ${ONEMORE} VBLOCK_A:/dev/urandom VBLOCK_B:/dev/zero
INFILES="${INFILES} ${ONEMORE}"

count=0
for infile in $INFILES; do

  base=${infile##*/}

  : $(( count++ ))
  echo -n "$count " 1>&3

  outfile=${TMP}.${base}.new
  loemid="loem"
  loemdir=${TMP}.${base}_dir

  mkdir -p ${loemdir}

  # grep for existing sha1sums (skipping root & recovery keys)
  ${FUTILITY} show ${infile} | grep sha1sum \
    | sed -e 's/.*: \+//' | tail -n 4 > ${TMP}.${base}.sha.orig

  # resign_firmwarefd.sh works on BIOS image files. The args are:
  #
  #   infile
  #   outfile
  #   firmware_datakey
  #   firmware_keyblock
  #   dev_firmware_datakey   (these are only used if RW A & RW B differ)
  #   dev_firmware_keyblock
  #   kernel_subkey
  #   firmware_version
  #   preamble_flag
  #   loem_output_dir        (optional: dir for copy of new vblocks)
  #   loemid                 (optional: copy new vblocks using this name)
  #
  #OLD  ${BINDIR}/resign_firmwarefd.sh \
  #OLD    ${infile} \
  #OLD    ${outfile} \
  #OLD    ${KEYDIR}/firmware_data_key.vbprivk \
  #OLD    ${KEYDIR}/firmware.keyblock \
  #OLD    ${KEYDIR}/dev_firmware_data_key.vbprivk \
  #OLD    ${KEYDIR}/dev_firmware.keyblock \
  #OLD    ${KEYDIR}/kernel_subkey.vbpubk \
  #OLD    14 \
  #OLD    9 \
  #OLD    ${loemdir} \
  #OLD    ${loemid}

  ${FUTILITY} sign \
    -s ${KEYDIR}/firmware_data_key.vbprivk \
    -b ${KEYDIR}/firmware.keyblock \
    -S ${KEYDIR}/dev_firmware_data_key.vbprivk \
    -B ${KEYDIR}/dev_firmware.keyblock \
    -k ${KEYDIR}/kernel_subkey.vbpubk \
    -v 14 \
    -f 9 \
    -d ${loemdir} \
    -l ${loemid} \
    ${infile} ${outfile}

  # check the firmware version and preamble flags
  m=$(${FUTILITY} show ${outfile} | \
    egrep 'Firmware version: +14$|Preamble flags: +9$' | wc -l)
  [ "$m" = "4" ]

  # check the sha1sums
  ${FUTILITY} show ${outfile} | grep sha1sum \
    | sed -e 's/.*: \+//' > ${TMP}.${base}.sha.new
  cmp ${SCRIPTDIR}/data_${base}_expect.txt ${TMP}.${base}.sha.new

  # and the LOEM stuff
  ${FUTILITY} show ${loemdir}/*.${loemid} | grep sha1sum \
    | sed -e 's/.*: \+//' > ${loemdir}/loem.sha.new
  # the vblocks don't have root or recovery keys
  tail -4 ${SCRIPTDIR}/data_${base}_expect.txt > ${loemdir}/sha.expect
  cmp ${loemdir}/sha.expect ${loemdir}/loem.sha.new

done

# Make sure that the BIOS with the good vblocks signed the right size.
GOOD_OUT=${TMP}.${GOOD_VBLOCKS##*/}.new
MORE_OUT=${TMP}.${ONEMORE##*/}.new

${FUTILITY} show ${GOOD_OUT} \
  | awk '/Firmware body size:/ {print $4}' > ${TMP}.good.body
${FUTILITY} dump_fmap -p ${GOOD_OUT} \
  | awk '/FW_MAIN_/ {print $3}' > ${TMP}.good.fw_main
# This should fail because they're different
if cmp ${TMP}.good.body ${TMP}.good.fw_main; then false; fi

# Make sure that the BIOS with the bad vblocks signed the whole fw body
${FUTILITY} show ${MORE_OUT} \
  | awk '/Firmware body size:/ {print $4}' > ${TMP}.onemore.body
${FUTILITY} dump_fmap -p ${MORE_OUT} \
  | awk '/FW_MAIN_/ {print $3}' > ${TMP}.onemore.fw_main
# These should match
cmp ${TMP}.onemore.body ${TMP}.onemore.fw_main
cmp ${TMP}.onemore.body ${TMP}.good.fw_main

# cleanup
rm -rf ${TMP}* ${ONEMORE}
exit 0
