// SPDX-License-Identifier: GPL-2.0
/*
 * Unisoc UMS9230 clock U-Boot driver
 *
 * Copyright (C) 2024 TODO
 *
 * Based on the Linux driver
 * Copyright (C) 2017 Spreadtrum, Inc.
 * Author: Chunyan Zhang <chunyan.zhang@spreadtrum.com>
 */

#include <asm/io.h>
#include <dm.h>

#include "gate.h"

static void clk_gate_toggle(struct sprd_gate_priv *priv,
			    const struct sprd_gate *sg, bool en)
{
	unsigned int reg;

	reg = readl(priv->base + sg->reg);

	if (en)
		reg |= sg->enable_mask;
	else
		reg &= ~sg->enable_mask;

	writel(reg, priv->base + sg->reg);
}

static void clk_sc_gate_toggle(struct sprd_gate_priv *priv,
			       const struct sprd_gate *sg, bool en)
{
	unsigned int offset;

	/*
	 * Each set/clear gate clock has three registers:
	 * reg			- base register
	 * reg + offset		- set register
	 * reg + 2 * offset	- clear register
	 */
	offset = en ? sg->sc_offset : sg->sc_offset * 2;

	writel(sg->enable_mask, priv->base + sg->reg + offset);
}

static int sprd_gate_disable(struct clk *clk)
{
	struct sprd_gate_priv *priv = dev_get_priv(clk->dev);

	if (clk->id >= priv->desc->num_gate_clks)
		return -EINVAL;

	clk_gate_toggle(priv, &priv->desc->gate_clks[clk->id], false);

	return 0;
}

static int sprd_gate_enable(struct clk *clk)
{
	struct sprd_gate_priv *priv = dev_get_priv(clk->dev);

	if (clk->id >= priv->desc->num_gate_clks)
		return -EINVAL;

	clk_gate_toggle(priv, &priv->desc->gate_clks[clk->id], true);

	return 0;
}

static int sprd_sc_gate_disable(struct clk *clk)
{
	struct sprd_gate_priv *priv = dev_get_priv(clk->dev);

	if (clk->id >= priv->desc->num_gate_clks)
		return -EINVAL;

	clk_sc_gate_toggle(priv, &priv->desc->gate_clks[clk->id], false);

	return 0;
}

static int sprd_sc_gate_enable(struct clk *clk)
{
	struct sprd_gate_priv *priv = dev_get_priv(clk->dev);

	if (clk->id >= priv->desc->num_gate_clks)
		return -EINVAL;

	clk_sc_gate_toggle(priv, &priv->desc->gate_clks[clk->id], true);

	return 0;
}

const struct clk_ops sprd_gate_ops = {
	.disable	= sprd_gate_disable,
	.enable		= sprd_gate_enable,
};

const struct clk_ops sprd_sc_gate_ops = {
	.disable	= sprd_sc_gate_disable,
	.enable		= sprd_sc_gate_enable,
};

int sprd_gate_probe(struct udevice *dev)
{
	struct sprd_gate_priv *priv = dev_get_priv(dev);

	priv->base = dev_read_addr_ptr(dev);
	if (!priv->base)
		return -ENOENT;

	priv->desc = (struct sprd_gate_desc *)dev_get_driver_data(dev);

	return 0;
}
