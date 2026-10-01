/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _BC250_SMU_H_
#define _BC250_SMU_H_

#include <linux/types.h>
#include <linux/device.h>
#include <linux/pci.h>

/* PCI Root Bridge SMN config aperture */
#define SMN_ADDR_REG                0xB8
#define SMN_DATA_REG                0xBC

/* SMU Queue 3 Mailbox registers in SMN space */
#define SMU_Q3_CMD_ADDR             0x03B10A20
#define SMU_Q3_RSP_ADDR             0x03B10A80
#define SMU_Q3_ARG_ADDR             0x03B10A88

/* SMU Queue 3 configuration limits */
#define SMU_Q3_MAX_ARGS             6
#define SMU_Q3_TIMEOUT_MS           100
#define SMU_Q3_POLL_SLEEP_MIN_US    200
#define SMU_Q3_POLL_SLEEP_MAX_US    500

/* SMU response codes */
#define SMU_RETURN_OK               0x01
#define SMU_RETURN_REJECTED_BUSY    0xFC
#define SMU_RETURN_REJECTED_PREREQ  0xFD
#define SMU_RETURN_UNKNOWN_CMD      0xFE
#define SMU_RETURN_FAILED           0xFF

/* Queue 3 Message IDs */
#define SMU_MSG_READ_VRAM_TEMP      0x05
#define SMU_MSG_SEC_SET_WRITE_PTR   0x28
#define SMU_MSG_SEC_WRITE_THROUGH32 0x29
#define SMU_MSG_SEC_SMN_READ32      0x2A

/* Probe address used to test secure/debug access */
#define SMU_PROBE_ADDR              0x0005A870

/* Low-level SMN and SMU functions (bc250_smu.c) */
int bc250_smn_read(struct pci_dev *pdev, u32 reg, u32 *val);
int bc250_smn_write(struct pci_dev *pdev, u32 reg, u32 val);
int bc250_smu_send_q3_raw(struct pci_dev *pdev, u32 msg_id,
			  const u32 *args, int num_args,
			  u32 *resp_status, u32 *resp_arg0);
int bc250_smu_send_q3(struct pci_dev *pdev, u32 msg_id,
		      const u32 *args, int num_args, u32 *resp_arg0);
int bc250_smu_write32(struct pci_dev *pdev, u32 addr, u32 val);

/* SMU patch and verification entry point (bc250_smu_patch.c) */
int bc250_smu_patch(struct device *dev, struct pci_dev *pdev, bool auto_patch);

#endif /* _BC250_SMU_H_ */
