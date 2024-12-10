// SPDX-License-Identifier: GPL-2.0
/*
 * Spreadtrum gate clock U-Boot driver
 *
 * Copyright (C) 2024 TODO
 */

#ifndef _SPRD_GATE_H_
#define _SPRD_GATE_H_

#include <clk-uclass.h>

struct sprd_gate {
	u32 reg;
	u32 enable_mask;
	u16 sc_offset;
};

struct sprd_gate_desc {
	const struct sprd_gate *gate_clks;
	unsigned int num_gate_clks;
};

struct sprd_gate_priv {
	void __iomem *base;
	const struct sprd_gate_desc *desc;
};

#define SPRD_GATE_CLK(_id, _reg, _sc_offset, _enable_mask)		\
	[CLK_ ## _id] = {						\
		.reg		= _reg,					\
		.enable_mask	= _enable_mask,				\
		.sc_offset	= _sc_offset,				\
	}

extern const struct clk_ops sprd_gate_ops;
extern const struct clk_ops sprd_sc_gate_ops;

int sprd_gate_probe(struct udevice *dev);

#endif /* _SPRD_GATE_H_ */
