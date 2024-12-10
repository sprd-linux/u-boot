// SPDX-License-Identifier: GPL-2.0-only

#include <clk.h>
#include <dm.h>
#include <dm/device_compat.h>
#include <ufs.h>

#include "ufs.h"

struct ufs_sprd_priv {
	struct clk_bulk clks;
};

static int ufs_sprd_init(struct ufs_hba *hba)
{
	struct ufs_sprd_priv *priv = dev_get_priv(hba->dev);
	int ret;

	ret = clk_get_bulk(hba->dev, &priv->clks);
	if (ret) {
		dev_err(hba->dev, "failed to get clocks: %d\n", ret);
		return ret;
	}

	return 0;
}

static int ufs_sprd_hce_enable_notify(struct ufs_hba *hba,
				      enum ufs_notify_change_status status)
{
	struct ufs_sprd_priv *priv = dev_get_priv(hba->dev);
	int ret = 0;

	switch (status) {
	case PRE_CHANGE:
		ret = clk_enable_bulk(&priv->clks);
		if (ret)
			dev_err(hba->dev, "failed to enable clocks: %d\n", ret);
		break;
	default:
		break;
	}

	return ret;
}

static int ufs_sprd_link_startup_notify(struct ufs_hba *hba,
					enum ufs_notify_change_status status)
{
	return 0;
}

static struct ufs_hba_ops ufs_sprd_hba_ops = {
	.init			= ufs_sprd_init,
	.hce_enable_notify	= ufs_sprd_hce_enable_notify,
	.link_startup_notify	= ufs_sprd_link_startup_notify,
};

static int ufs_sprd_probe(struct udevice *dev)
{
	int ret;

	ret = ufshcd_probe(dev, &ufs_sprd_hba_ops);
	if (ret) {
		dev_err(dev, "ufshcd_probe() failed with %d\n", ret);
		return ret;
	}

	return 0;
}

static int ufs_sprd_bind(struct udevice *dev)
{
	struct udevice *scsi_dev;

	return ufs_scsi_bind(dev, &scsi_dev);
}

static const struct udevice_id ufs_sprd_ids[] = {
	{ .compatible = "sprd,ums9230-ufs" },
	{ }
};

U_BOOT_DRIVER(ufs_sprd_ums9230) = {
	.name		= "ufs-sprd-ums9230",
	.id		= UCLASS_UFS,
	.of_match	= ufs_sprd_ids,
	.probe		= ufs_sprd_probe,
	.bind		= ufs_sprd_bind,
	.priv_auto	= sizeof(struct ufs_sprd_priv),
};
