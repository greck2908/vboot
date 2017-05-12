/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * EC software sync routines for vboot
 */

#include "2sysincludes.h"
#include "2common.h"
#include "2misc.h"
#include "2nvstorage.h"

#include "sysincludes.h"
#include "ec_sync.h"
#include "vboot_api.h"
#include "vboot_common.h"
#include "vboot_display.h"
#include "vboot_kernel.h"

VbError_t ec_sync_all(struct vb2_context *ctx, struct VbCommonParams *cparams)
{
	VbSharedDataHeader *shared =
		(VbSharedDataHeader *)cparams->shared_data_blob;
	VbAuxFwUpdateSeverity_t fw_update;
	VbError_t rv;

	rv = ec_sync_check_aux_fw(ctx, cparams, &fw_update);
	if (rv)
		return rv;

	/* Do EC sync phase 1; this determines if we need an update */
	VbError_t phase1_rv = ec_sync_phase1(ctx, cparams);
	int need_wait_screen = ec_will_update_slowly(ctx, cparams) ||
		(fw_update == VB_AUX_FW_SLOW_UPDATE);

	/*
	 * Check if we need to reboot to load the VGA Option ROM before we can
	 * display the WAIT screen.
	 *
	 * Do this before we check if ec_sync_phase1() requires a reboot for
	 * some other reason, since there's no reason to reboot twice.
	 */
	int reboot_for_oprom = (need_wait_screen &&
				shared->flags & VBSD_OPROM_MATTERS &&
				!(shared->flags & VBSD_OPROM_LOADED));
	if (reboot_for_oprom) {
		VB2_DEBUG("%s: Reboot to load VGA Option ROM\n", __func__);
		vb2_nv_set(ctx, VB2_NV_OPROM_NEEDED, 1);
	}

	/* Reboot if phase 1 needed it, or if we need to load VGA Option ROM */
	if (phase1_rv)
		return VBERROR_EC_REBOOT_TO_RO_REQUIRED;
	if (reboot_for_oprom)
		return VBERROR_VGA_OPROM_MISMATCH;

	/* Display the wait screen if we need it */
	if (need_wait_screen) {
		VB2_DEBUG("%s: EC is slow. Show WAIT screen.\n", __func__);
		VbDisplayScreen(ctx, cparams, VB_SCREEN_WAIT, 0);
	}

	/*
	 * Do EC sync phase 2; this applies the update and/or jumps to the
	 * correct EC image.
	 */
	rv = ec_sync_phase2(ctx, cparams);
	if (rv)
		return rv;

	/*
	 * Do software sync for devices tunneled throught the EC.
	 */
	if (fw_update != VB_AUX_FW_NO_UPDATE) {
		rv = VbExUpdateAuxFw();
		if (rv)
			return rv;
	}

	/*
	 * Reboot to unload VGA Option ROM if:
	 * - we displayed the wait screen
	 * - the system has slow EC update flag set
	 * - the VGA Option ROM was needed and loaded
	 * - the system is NOT in developer mode (that'll also need the ROM)
	 */
	if (need_wait_screen &&
	    (shared->flags & VBSD_OPROM_MATTERS) &&
	    (shared->flags & VBSD_OPROM_LOADED) &&
	    !(shared->flags & VBSD_BOOT_DEV_SWITCH_ON)) {
		VB2_DEBUG("%s: Reboot to unload VGA Option ROM\n", __func__);
		vb2_nv_set(ctx, VB2_NV_OPROM_NEEDED, 0);
		return VBERROR_VGA_OPROM_MISMATCH;
	}

	/* Do EC sync phase 3; this completes sync and handles battery cutoff */
	rv = ec_sync_phase3(ctx, cparams);
	if (rv)
		return rv;

	return VBERROR_SUCCESS;
}
