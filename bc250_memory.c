// SPDX-License-Identifier: GPL-2.0-or-later OR MIT
/*
 * bc250_memory.c - Linux hwmon driver for AMD BC-250 GDDR6 memory telemetry
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/pci.h>
#include <linux/hwmon.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/dmi.h>
#include <linux/jiffies.h>
#include <linux/limits.h>

#include "bc250_smu.h"
#include "bc250_smu.c"
#include "bc250_smu_patch.c"

#define DRV_NAME "bc250_memory"

#define BC250_MEMORY_NUM_CHIPS      8
#define BC250_CACHE_TIMEOUT_MS      1000

/* JEDEC MR3 temperature encoding: Temp (°C) = (code * 2) - 40 */
#define JEDEC_MR3_TEMP_MASK         0xFF
#define JEDEC_MR3_TEMP_SCALE        2
#define JEDEC_MR3_TEMP_OFFSET       40
#define MILLICELSIUS_PER_CELSIUS    1000

/* Hwmon channel indices */
#define BC250_CHAN_HOTSPOT          0
#define BC250_CHAN_AVERAGE          1
#define BC250_CHAN_CHIP_BASE        2

static bool auto_patch = true;
module_param(auto_patch, bool, 0444);
MODULE_PARM_DESC(auto_patch, "Automatically patch SMU firmware for memory temperature readout (default: true)");

static bool force = false;
module_param(force, bool, 0444);
MODULE_PARM_DESC(force, "Force loading even if DMI does not match AMD BC-250 (default: false)");

struct bc250_memory_data {
	struct pci_dev *root_pdev;
	struct mutex lock;
	unsigned long last_updated; /* jiffies */
	bool valid;
	long chip_temp[BC250_MEMORY_NUM_CHIPS]; /* millidegrees C */
	long hotspot_temp;
	long average_temp;
};

static const char * const bc250_memory_labels[] = {
	"VRAM Hotspot",
	"VRAM Average",
	"VRAM Chip 0",
	"VRAM Chip 1",
	"VRAM Chip 2",
	"VRAM Chip 3",
	"VRAM Chip 4",
	"VRAM Chip 5",
	"VRAM Chip 6",
	"VRAM Chip 7",
};

static int bc250_smu_read_chip_temp(struct pci_dev *pdev, u32 chip, long *temp_mc)
{
	u32 resp = 0;
	long temp_c;
	int ret;

	ret = bc250_smu_send_q3(pdev, SMU_MSG_READ_VRAM_TEMP, &chip, 1, &resp);
	if (ret)
		return ret;

	/* JEDEC MR3 encoding: Temp (°C) = ((code & 0xFF) * 2) - 40 */
	temp_c = ((long)(resp & JEDEC_MR3_TEMP_MASK) * JEDEC_MR3_TEMP_SCALE) - JEDEC_MR3_TEMP_OFFSET;
	*temp_mc = temp_c * MILLICELSIUS_PER_CELSIUS;
	return 0;
}

static int bc250_memory_update_temps(struct bc250_memory_data *data)
{
	int ret = 0;
	int i;
	long sum = 0;
	long hotspot = LONG_MIN;

	mutex_lock(&data->lock);

	if (data->valid &&
	    time_before(jiffies, data->last_updated + msecs_to_jiffies(BC250_CACHE_TIMEOUT_MS)))
		goto out;

	for (i = 0; i < BC250_MEMORY_NUM_CHIPS; i++) {
		ret = bc250_smu_read_chip_temp(data->root_pdev, i, &data->chip_temp[i]);
		if (ret) {
			data->valid = false;
			goto out;
		}

		sum += data->chip_temp[i];
		if (data->chip_temp[i] > hotspot)
			hotspot = data->chip_temp[i];
	}

	data->hotspot_temp = hotspot;
	data->average_temp = sum / BC250_MEMORY_NUM_CHIPS;
	data->last_updated = jiffies;
	data->valid = true;

out:
	mutex_unlock(&data->lock);
	return ret;
}

static umode_t bc250_memory_is_visible(const void *drvdata, enum hwmon_sensor_types type,
				       u32 attr, int channel)
{
	if (type == hwmon_temp &&
	    (attr == hwmon_temp_input || attr == hwmon_temp_label) &&
	    channel >= 0 && channel < ARRAY_SIZE(bc250_memory_labels))
		return 0444;

	return 0;
}

static int bc250_memory_read(struct device *dev, enum hwmon_sensor_types type,
			     u32 attr, int channel, long *val)
{
	struct bc250_memory_data *data = dev_get_drvdata(dev);
	int ret;

	if (type != hwmon_temp || attr != hwmon_temp_input)
		return -EOPNOTSUPP;

	ret = bc250_memory_update_temps(data);
	if (ret)
		return ret;

	if (channel == BC250_CHAN_HOTSPOT)
		*val = data->hotspot_temp;
	else if (channel == BC250_CHAN_AVERAGE)
		*val = data->average_temp;
	else if (channel >= BC250_CHAN_CHIP_BASE &&
		 channel < BC250_CHAN_CHIP_BASE + BC250_MEMORY_NUM_CHIPS)
		*val = data->chip_temp[channel - BC250_CHAN_CHIP_BASE];
	else
		return -EOPNOTSUPP;

	return 0;
}

static int bc250_memory_read_string(struct device *dev, enum hwmon_sensor_types type,
				    u32 attr, int channel, const char **str)
{
	if (type != hwmon_temp || attr != hwmon_temp_label)
		return -EOPNOTSUPP;

	if (channel >= 0 && channel < ARRAY_SIZE(bc250_memory_labels)) {
		*str = bc250_memory_labels[channel];
		return 0;
	}

	return -EOPNOTSUPP;
}

static const struct hwmon_channel_info * const bc250_memory_info[] = {
	HWMON_CHANNEL_INFO(temp,
			   HWMON_T_INPUT | HWMON_T_LABEL,  /* temp1: Hotspot */
			   HWMON_T_INPUT | HWMON_T_LABEL,  /* temp2: Average */
			   HWMON_T_INPUT | HWMON_T_LABEL,  /* temp3: Chip 0 */
			   HWMON_T_INPUT | HWMON_T_LABEL,  /* temp4: Chip 1 */
			   HWMON_T_INPUT | HWMON_T_LABEL,  /* temp5: Chip 2 */
			   HWMON_T_INPUT | HWMON_T_LABEL,  /* temp6: Chip 3 */
			   HWMON_T_INPUT | HWMON_T_LABEL,  /* temp7: Chip 4 */
			   HWMON_T_INPUT | HWMON_T_LABEL,  /* temp8: Chip 5 */
			   HWMON_T_INPUT | HWMON_T_LABEL,  /* temp9: Chip 6 */
			   HWMON_T_INPUT | HWMON_T_LABEL), /* temp10: Chip 7 */
	NULL
};

static const struct hwmon_ops bc250_memory_hwmon_ops = {
	.is_visible  = bc250_memory_is_visible,
	.read        = bc250_memory_read,
	.read_string = bc250_memory_read_string,
};

static const struct hwmon_chip_info bc250_memory_chip_info = {
	.ops  = &bc250_memory_hwmon_ops,
	.info = bc250_memory_info,
};

static int bc250_memory_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct bc250_memory_data *data;
	struct device *hwmon_dev;
	int ret;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	mutex_init(&data->lock);

	/* Find PCI Host Bridge (0000:00:00.0) */
	data->root_pdev = pci_get_domain_bus_and_slot(0, 0, PCI_DEVFN(0, 0));
	if (!data->root_pdev) {
		dev_err(dev, "failed to find PCI root complex (0000:00:00.0)\n");
		return -ENODEV;
	}

	mutex_lock(&data->lock);
	ret = bc250_smu_patch(dev, data->root_pdev, auto_patch);
	mutex_unlock(&data->lock);
	if (ret) {
		pci_dev_put(data->root_pdev);
		return ret;
	}

	hwmon_dev = devm_hwmon_device_register_with_info(dev, DRV_NAME,
							 data,
							 &bc250_memory_chip_info,
							 NULL);
	if (IS_ERR(hwmon_dev)) {
		pci_dev_put(data->root_pdev);
		return PTR_ERR(hwmon_dev);
	}

	platform_set_drvdata(pdev, data);
	return 0;
}

static void bc250_memory_remove(struct platform_device *pdev)
{
	struct bc250_memory_data *data = platform_get_drvdata(pdev);

	if (data && data->root_pdev)
		pci_dev_put(data->root_pdev);
}

static const struct dmi_system_id bc250_dmi_table[] = {
	{
		.matches = {
			DMI_MATCH(DMI_BOARD_NAME, "AMD BC-250"),
		},
	},
	{ }
};
MODULE_DEVICE_TABLE(dmi, bc250_dmi_table);

static struct platform_driver bc250_memory_driver = {
	.driver = {
		.name = DRV_NAME,
	},
	.probe  = bc250_memory_probe,
	.remove = bc250_memory_remove,
};

static struct platform_device *bc250_memory_pdev;

static int __init bc250_memory_init(void)
{
	int ret;

	if (!force && !dmi_check_system(bc250_dmi_table)) {
		pr_err(DRV_NAME ": unsupported hardware (board name is not AMD BC-250)\n");
		return -ENODEV;
	}

	ret = platform_driver_register(&bc250_memory_driver);
	if (ret)
		return ret;

	bc250_memory_pdev = platform_device_register_simple(DRV_NAME, -1, NULL, 0);
	if (IS_ERR(bc250_memory_pdev)) {
		platform_driver_unregister(&bc250_memory_driver);
		return PTR_ERR(bc250_memory_pdev);
	}

	return 0;
}

static void __exit bc250_memory_exit(void)
{
	platform_device_unregister(bc250_memory_pdev);
	platform_driver_unregister(&bc250_memory_driver);
}

module_init(bc250_memory_init);
module_exit(bc250_memory_exit);

MODULE_AUTHOR("Liam McLoughlin <hexxeh@hexxeh.net>");
MODULE_DESCRIPTION("AMD BC-250 GDDR6 memory hwmon driver");
MODULE_LICENSE("Dual MIT/GPL");
MODULE_VERSION("1.0.0");
