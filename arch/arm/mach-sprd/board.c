// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2024 Otto Pflüger
 */

#include <asm/armv8/mmu.h>
#include <asm/global_data.h>
#include <asm/system.h>
#include <linux/sizes.h>

DECLARE_GLOBAL_DATA_PTR;

static struct mm_region unisoc_mem_map[] = {
	{
		/* DRAM */
		.virt = 0x80000000,
		.phys = 0x80000000,
		.size = 8UL * SZ_1G,
		.attrs = PTE_BLOCK_MEMTYPE(MT_NORMAL) |
			 PTE_BLOCK_OUTER_SHARE
	}, {
		/* I/O */
		.virt = 0x00000000,
		.phys = 0x00000000,
		.size = 0x80000000,
		.attrs = PTE_BLOCK_MEMTYPE(MT_DEVICE_NGNRNE) |
			 PTE_BLOCK_NON_SHARE |
			 PTE_BLOCK_PXN | PTE_BLOCK_UXN
	}, {
		0,
	}
};
struct mm_region *mem_map = unisoc_mem_map;

int dram_init(void)
{
	int ret;

	ret = fdtdec_setup_memory_banksize();
	if (ret)
		return ret;

	return fdtdec_setup_mem_size_base();
}

int dram_init_banksize(void)
{
	gd->bd->bi_dram[0].start = gd->ram_base;
	gd->bd->bi_dram[0].size = gd->ram_size;

	return 0;
}

int board_init(void)
{
	/* HACK: Set SDIO 2x clock source to RPLL */
	*((u32*)0x20010074) = 4;
	return 0;
}
