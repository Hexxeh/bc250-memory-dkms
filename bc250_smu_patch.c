// SPDX-License-Identifier: GPL-2.0-or-later OR MIT
/*
 * bc250_smu_patch.c - SMU unlock check and custom firmware payload patcher
 */

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/pci.h>

#include "bc250_smu.h"

#define DRV_NAME "bc250_memory"

/* SMU payload and patching parameters */
#define PAYLOAD_START               0x0003AA9C
#define QUEUE_3_MSG_5_HANDLER       0x0003AAC4
#define QUEUE_3_MSG_5_HANDLER_REG   0x0000748C

/*
 * smu_payload is derived from SMUPayload.bin in:
 *   https://github.com/pan-Rijovich/bc250-memory-temperature
 *
 * Copyright (c) 2026 pan-Rijovich, bc250-collective
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
static const u32 smu_payload[] = {
	0x00053a24, 0x00053a1c, 0x00053a20, 0x00053a2c,
	0x00001234, 0x00000ffc, 0x00002a74, 0x000029f8,
	0x00000fe4, 0x00000fa8, 0x20004136, 0xf98120a2,
	0x0008e0ff, 0xc0fff3b1, 0x2d0c014a, 0xb4b00dcd,
	0x810a0c20, 0x08e0fff5, 0xffeeb100, 0xa0c22d0c,
	0x20b4b005, 0xf0810a0c, 0x0008e0ff, 0x30ffeb31,
	0x2c0c2034, 0xa220b330, 0xec8100a0, 0x0008e0ff,
	0x31ee5a66, 0x3430ffe6, 0xbd2c0c20, 0x810a0c03,
	0x08e0ffe7, 0xffe24100, 0x0ced1a47, 0x20b3302c,
	0xe2810a0c, 0x0008e0ff, 0x02ad0abd, 0xe0ffe181,
	0x1b0c0008, 0xdf8102ad, 0x0008e0ff, 0x0000f01d,
};

/*
 * Check if SMU debug / secure access is enabled.
 * Matches secure_access_enabled() in bc250-memory-temperature (unlock.py / api_q3.py):
 * Sends Queue 3 Message 0x2A (sec_smn_read32). If gated commands are locked,
 * the SMU returns SMU_RETURN_REJECTED_PREREQ (0xFD).
 *
 * Returns:
 *   1 if unlocked (SMU_RETURN_OK)
 *   0 if locked (SMU_RETURN_REJECTED_PREREQ)
 *   negative error code on communication/timeout error
 */
static int bc250_smu_is_unlocked(struct pci_dev *pdev)
{
	u32 args[1] = { SMU_PROBE_ADDR };
	u32 status = 0;
	int ret;

	ret = bc250_smu_send_q3_raw(pdev, SMU_MSG_SEC_SMN_READ32, args, 1, &status, NULL);
	if (ret)
		return ret;

	if (status == SMU_RETURN_REJECTED_PREREQ)
		return 0;

	if (status == SMU_RETURN_OK)
		return 1;

	pr_warn(DRV_NAME ": unexpected status 0x%02X from unlock probe\n", status);
	return -EIO;
}

static int bc250_smu_test_vram_read(struct pci_dev *pdev)
{
	u32 chip = 0;
	u32 resp = 0;
	int ret;

	ret = bc250_smu_send_q3(pdev, SMU_MSG_READ_VRAM_TEMP, &chip, 1, &resp);
	if (ret)
		return ret;

	return 0;
}

static int bc250_smu_apply_payload(struct device *dev, struct pci_dev *pdev)
{
	int ret, i;
	u32 addr;

	dev_info(dev, "writing custom SMU payload (%zu words)...\n",
		 ARRAY_SIZE(smu_payload));

	addr = PAYLOAD_START;
	for (i = 0; i < ARRAY_SIZE(smu_payload); i++) {
		ret = bc250_smu_write32(pdev, addr, smu_payload[i]);
		if (ret) {
			dev_err(dev, "failed to write payload word %d to 0x%08x: %d\n",
				i, addr, ret);
			return ret;
		}
		addr += sizeof(u32);
	}

	dev_info(dev, "redirecting Queue 3 Msg 5 handler to 0x%08x...\n",
		 QUEUE_3_MSG_5_HANDLER);
	ret = bc250_smu_write32(pdev, QUEUE_3_MSG_5_HANDLER_REG, QUEUE_3_MSG_5_HANDLER);
	if (ret) {
		dev_err(dev, "failed to update handler vector register: %d\n", ret);
		return ret;
	}

	/* Verify patch with a test read */
	ret = bc250_smu_test_vram_read(pdev);
	if (ret) {
		dev_err(dev, "patch applied but verification read failed: %d\n", ret);
		return ret;
	}

	dev_info(dev, "SMU payload applied and verified successfully\n");
	return 0;
}

/*
 * - Checks if SMU is already patched (Queue 3 Msg 5 responding)
 * - Validates auto_patch module parameter
 * - Verifies SMU debug access is unlocked
 * - Injects payload and hooks handler vector
 */
int bc250_smu_patch(struct device *dev, struct pci_dev *pdev, bool auto_patch)
{
	int ret;

	/* 1. Check if SMU Queue 3 Msg 5 is already active */
	ret = bc250_smu_test_vram_read(pdev);
	if (ret == 0) {
		return 0;
	}

	/* 2. If not active and auto_patch is disabled, bail out gracefully */
	if (!auto_patch) {
		dev_warn(dev, "SMU is not patched and auto_patch is disabled, bailing out\n");
		return -ENODEV;
	}

	/* 3. Check if SMU debug access is unlocked before attempting to patch */
	ret = bc250_smu_is_unlocked(pdev);
	if (ret == 0) {
		dev_warn(dev, "SMU debug access is locked (0xFD) - BIOS unlock required to patch firmware, bailing out\n");
		return -ENODEV;
	} else if (ret < 0) {
		dev_warn(dev, "failed to query SMU unlock status (%d), bailing out\n", ret);
		return ret;
	}

	/* 4. SMU is unlocked: apply patch */
	ret = bc250_smu_apply_payload(dev, pdev);
	if (ret) {
		dev_warn(dev, "failed to patch SMU firmware (%d), bailing out\n", ret);
		return ret;
	}

	return 0;
}
