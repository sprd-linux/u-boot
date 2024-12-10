// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2024 Otto Pflüger
 */

#include <dm.h>
#include <serial.h>

#define SPRD_TXD		0x0000
#define SPRD_RXD		0x0004

#define SPRD_STS1		0x000C
#define SPRD_RX_FIFO_CNT_MASK	GENMASK(7, 0)
#define SPRD_TX_FIFO_CNT_MASK	GENMASK(15, 8)

struct sprd_serial_data {
	phys_addr_t base;
};

static int sprd_serial_putc(struct udevice *dev, const char c)
{
	struct sprd_serial_data *priv = dev_get_priv(dev);

	if (readl(priv->base + SPRD_STS1) & SPRD_TX_FIFO_CNT_MASK)
		return -EAGAIN;

	writeb(c, priv->base + SPRD_TXD);

	return 0;
}

static int sprd_serial_pending(struct udevice *dev, bool input)
{
	struct sprd_serial_data *priv = dev_get_priv(dev);
	u32 sts1 = readl(priv->base + SPRD_STS1);

	return !!(sts1 & (input ? SPRD_RX_FIFO_CNT_MASK : SPRD_TX_FIFO_CNT_MASK));
}

static int sprd_serial_getc(struct udevice *dev)
{
	struct sprd_serial_data *priv = dev_get_priv(dev);

	if (!(readl(priv->base + SPRD_STS1) & SPRD_RX_FIFO_CNT_MASK))
		return -EAGAIN;

	return readb(priv->base + SPRD_RXD);
}

static const struct dm_serial_ops sprd_serial_ops = {
	.putc = sprd_serial_putc,
	.pending = sprd_serial_pending,
	.getc = sprd_serial_getc,
};

static int sprd_serial_probe(struct udevice *dev)
{
	return 0;
}

static int sprd_serial_of_to_plat(struct udevice *dev)
{
	struct sprd_serial_data *priv = dev_get_priv(dev);

	priv->base = dev_read_addr(dev);
	if (priv->base == FDT_ADDR_T_NONE)
		return -EINVAL;

	return 0;
}

static const struct udevice_id sprd_serial_ids[] = {
	{ .compatible = "sprd,sc9836-uart" },
	{ }
};

U_BOOT_DRIVER(serial_sprd) = {
	.name		= "serial_sprd",
	.id		= UCLASS_SERIAL,
	.of_match	= sprd_serial_ids,
	.of_to_plat	= sprd_serial_of_to_plat,
	.priv_auto	= sizeof(struct sprd_serial_data),
	.probe		= sprd_serial_probe,
	.ops		= &sprd_serial_ops,
};
