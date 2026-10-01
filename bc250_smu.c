// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * bc250_smu.c - AMD BC-250 SMN aperture and SMU Queue 3 Mailbox routines
 */

#include <linux/delay.h>
#include <linux/jiffies.h>
#include <linux/pci.h>

#include "bc250_smu.h"

int bc250_smn_read(struct pci_dev *pdev, u32 reg, u32 *val)
{
	int ret;

	ret = pci_write_config_dword(pdev, SMN_ADDR_REG, reg);
	if (ret)
		return ret;
	return pci_read_config_dword(pdev, SMN_DATA_REG, val);
}

int bc250_smn_write(struct pci_dev *pdev, u32 reg, u32 val)
{
	int ret;

	ret = pci_write_config_dword(pdev, SMN_ADDR_REG, reg);
	if (ret)
		return ret;
	return pci_write_config_dword(pdev, SMN_DATA_REG, val);
}

int bc250_smu_send_q3_raw(struct pci_dev *pdev, u32 msg_id,
			  const u32 *args, int num_args,
			  u32 *resp_status, u32 *resp_arg0)
{
	int ret, i;
	u32 rsp = 0;
	unsigned long deadline;

	/* Clear response register */
	ret = bc250_smn_write(pdev, SMU_Q3_RSP_ADDR, 0);
	if (ret)
		return ret;

	/* Write arguments */
	for (i = 0; i < SMU_Q3_MAX_ARGS; i++) {
		u32 arg = (i < num_args) ? args[i] : 0;

		ret = bc250_smn_write(pdev, SMU_Q3_ARG_ADDR + (sizeof(u32) * i), arg);
		if (ret)
			return ret;
	}

	/* Send command */
	ret = bc250_smn_write(pdev, SMU_Q3_CMD_ADDR, msg_id);
	if (ret)
		return ret;

	/* Wait for response */
	deadline = jiffies + msecs_to_jiffies(SMU_Q3_TIMEOUT_MS);
	while (time_before(jiffies, deadline)) {
		ret = bc250_smn_read(pdev, SMU_Q3_RSP_ADDR, &rsp);
		if (ret)
			return ret;
		if (rsp != 0)
			break;
		usleep_range(SMU_Q3_POLL_SLEEP_MIN_US, SMU_Q3_POLL_SLEEP_MAX_US);
	}

	if (rsp == 0)
		return -ETIMEDOUT;

	if (resp_status)
		*resp_status = rsp;

	if (resp_arg0) {
		ret = bc250_smn_read(pdev, SMU_Q3_ARG_ADDR, resp_arg0);
		if (ret)
			return ret;
	}

	return 0;
}

int bc250_smu_send_q3(struct pci_dev *pdev, u32 msg_id,
		      const u32 *args, int num_args, u32 *resp_arg0)
{
	u32 status = 0;
	int ret;

	ret = bc250_smu_send_q3_raw(pdev, msg_id, args, num_args, &status, resp_arg0);
	if (ret)
		return ret;

	if (status != SMU_RETURN_OK)
		return -EIO;

	return 0;
}

int bc250_smu_write32(struct pci_dev *pdev, u32 addr, u32 val)
{
	int ret;

	ret = bc250_smu_send_q3(pdev, SMU_MSG_SEC_SET_WRITE_PTR, &addr, 1, NULL);
	if (ret)
		return ret;

	return bc250_smu_send_q3(pdev, SMU_MSG_SEC_WRITE_THROUGH32, &val, 1, NULL);
}
