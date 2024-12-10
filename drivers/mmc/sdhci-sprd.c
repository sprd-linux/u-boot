// SPDX-License-Identifier: GPL-2.0+

#include <clk.h>
#include <dm.h>
#include <dm/device_compat.h>
#include <sdhci.h>

#define  SDHCI_SPRD_INT_SIGNAL_MASK	0x1B7F410B

/*
 * According to the standard specification, BIT(3) of SDHCI_SOFTWARE_RESET is
 * reserved, and only used on Spreadtrum's design, the hardware cannot work
 * if this bit is cleared.
 * 1 : normal work
 * 0 : hardware reset
 */
#define SDHCI_HW_RESET_CARD		BIT(3)

#define SDHCI_SPRD_MAX_CUR		0xFFFFFF

struct sdhci_sprd_plat {
	struct mmc_config cfg;
	struct mmc mmc;
};

struct sdhci_sprd_priv {
	struct sdhci_host host;
	struct clk clk_sdio;
	struct clk clk_enable;
};

static u32 sdhci_sprd_readl(struct sdhci_host *host, int reg)
{
	if (unlikely(reg == SDHCI_MAX_CURRENT))
		return SDHCI_SPRD_MAX_CUR;

	return readl_relaxed(host->ioaddr + reg);
}

static void sdhci_sprd_writel(struct sdhci_host *host, u32 val, int reg)
{
	/* SDHCI_MAX_CURRENT is reserved on Spreadtrum's platform */
	if (unlikely(reg == SDHCI_MAX_CURRENT))
		return;

	if (unlikely(reg == SDHCI_SIGNAL_ENABLE || reg == SDHCI_INT_ENABLE))
		val = val & SDHCI_SPRD_INT_SIGNAL_MASK;

	writel_relaxed(val, host->ioaddr + reg);
}

static void sdhci_sprd_writew(struct sdhci_host *host, u16 val, int reg)
{
	/* SDHCI_BLOCK_COUNT is Read Only on Spreadtrum's platform */
	if (unlikely(reg == SDHCI_BLOCK_COUNT))
		return;

	writew_relaxed(val, host->ioaddr + reg);
}

static void sdhci_sprd_writeb(struct sdhci_host *host, u8 val, int reg)
{
	/*
	 * Since BIT(3) of SDHCI_SOFTWARE_RESET is reserved according to the
	 * standard specification, sdhci_reset() write this register directly
	 * without checking other reserved bits, that will clear BIT(3) which
	 * is defined as hardware reset on Spreadtrum's platform and clearing
	 * it by mistake will lead the card not work. So here we need to work
	 * around it.
	 */
	if (unlikely(reg == SDHCI_SOFTWARE_RESET)) {
		if (readb_relaxed(host->ioaddr + reg) & SDHCI_HW_RESET_CARD)
			val |= SDHCI_HW_RESET_CARD;
	}

	writeb_relaxed(val, host->ioaddr + reg);
}

struct sdhci_ops sdhci_sprd_ops = {
	.read_l = sdhci_sprd_readl,
	.write_l = sdhci_sprd_writel,
	.write_w = sdhci_sprd_writew,
	.write_b = sdhci_sprd_writeb,
};

static int sdhci_sprd_probe(struct udevice *dev)
{
	struct mmc_uclass_priv *upriv = dev_get_uclass_priv(dev);
	struct sdhci_sprd_plat *plat = dev_get_plat(dev);
	struct sdhci_sprd_priv *priv = dev_get_priv(dev);
	struct sdhci_host *host = &priv->host;
	int ret;

	ret = clk_get_by_name(dev, "sdio", &priv->clk_sdio);
	if (ret) {
		dev_err(dev, "failed to get 'sdio' clock: %d\n", ret);
		return ret;
	}

	ret = clk_get_by_name(dev, "enable", &priv->clk_enable);
	if (ret) {
		dev_err(dev, "failed to get 'enable' clock: %d\n", ret);
		return ret;
	}

	ret = clk_enable(&priv->clk_enable);
	if (ret) {
		dev_err(dev, "failed to enable clock: %d\n", ret);
		return ret;
	}

	ret = mmc_of_parse(dev, &plat->cfg);
	if (ret)
		return ret;

	host->quirks = SDHCI_QUIRK_USE_32BIT_BLK_CNT;
	host->max_clk = clk_round_rate(&priv->clk_sdio, ULONG_MAX);

	host->mmc = &plat->mmc;
	host->mmc->dev = dev;
	ret = sdhci_setup_cfg(&plat->cfg, host, 0, 0);
	if (ret)
		return ret;

	host->mmc->priv = &priv->host;
	upriv->mmc = host->mmc;

	return sdhci_probe(dev);
}

static int sdhci_sprd_remove(struct udevice *dev)
{
	struct sdhci_sprd_priv *priv = dev_get_priv(dev);

	clk_disable(&priv->clk_enable);

	return 0;
}

static int sdhci_sprd_of_to_plat(struct udevice *dev)
{
	struct sdhci_sprd_priv *priv = dev_get_priv(dev);
	struct sdhci_host *host = &priv->host;

	host->ops = &sdhci_sprd_ops;
	host->name = strdup(dev->name);
	host->ioaddr = dev_read_addr_ptr(dev);
	if (!host->ioaddr)
		return -EINVAL;

	return 0;
}

static int sdhci_sprd_bind(struct udevice *dev)
{
	struct sdhci_sprd_plat *plat = dev_get_plat(dev);

	return sdhci_bind(dev, &plat->mmc, &plat->cfg);
}

static const struct udevice_id sdhci_sprd_of_match[] = {
	{ .compatible = "sprd,sdhci-r11" },
	{ }
};

U_BOOT_DRIVER(sdhci_sprd_drv) = {
	.name		= "sdhci_sprd",
	.id		= UCLASS_MMC,
	.of_match	= sdhci_sprd_of_match,
	.of_to_plat	= sdhci_sprd_of_to_plat,
	.ops		= &sdhci_ops,
	.bind		= sdhci_sprd_bind,
	.probe		= sdhci_sprd_probe,
	.remove		= sdhci_sprd_remove,
	.priv_auto	= sizeof(struct sdhci_sprd_priv),
	.plat_auto	= sizeof(struct sdhci_sprd_plat),
};
