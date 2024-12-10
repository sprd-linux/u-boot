/*
 * Copyright (C) 2017 Spreadtrum Communications Inc.
 *
 * SPDX-License-Identifier: GPL-2.0
 */

#include <asm/io.h>
#include <dm.h>
#include <dm/device_compat.h>
#include <hwspinlock.h>
#include <linux/sizes.h>
#include <spi.h>

/* Registers definitions for ADI controller */
#define REG_ADI_CTRL0			0x4
#define REG_ADI_CHN_PRIL		0x8
#define REG_ADI_CHN_PRIH		0xc
#define REG_ADI_INT_EN			0x10
#define REG_ADI_INT_RAW			0x14
#define REG_ADI_INT_MASK		0x18
#define REG_ADI_INT_CLR			0x1c
#define REG_ADI_GSSI_CFG0		0x20
#define REG_ADI_GSSI_CFG1		0x24
#define REG_ADI_RD_CMD			0x28
#define REG_ADI_RD_DATA			0x2c
#define REG_ADI_ARM_FIFO_STS		0x30
#define REG_ADI_STS			0x34
#define REG_ADI_EVT_FIFO_STS		0x38
#define REG_ADI_ARM_CMD_STS		0x3c
#define REG_ADI_CHN_EN			0x40
#define REG_ADI_CHN_ADDR(id)		(0x44 + (id - 2) * 4)
#define REG_ADI_CHN_EN1			0x20c

/* Bits definitions for register REG_ADI_GSSI_CFG0 */
#define BIT_CLK_ALL_ON			BIT(30)

/* Bits definitions for register REG_ADI_RD_DATA */
#define BIT_RD_CMD_BUSY			BIT(31)
#define RD_ADDR_SHIFT			16
#define RD_VALUE_MASK			GENMASK(15, 0)
#define RD_ADDR_MASK			GENMASK(30, 16)

/* Bits definitions for register REG_ADI_ARM_FIFO_STS */
#define BIT_FIFO_FULL			BIT(11)
#define BIT_FIFO_EMPTY			BIT(10)

/*
 * ADI slave devices include RTC, ADC, regulator, charger, thermal and so on.
 * ADI supports 12/14bit address for r2p0, and additional 17bit for r3p0 or
 * later versions. Since bit[1:0] are zero, so the spec describe them as
 * 10/12/15bit address mode.
 * The 10bit mode supports sigle slave, 12/15bit mode supports 3 slave, the
 * high two bits is slave_id.
 * The slave devices address offset is 0x8000 for 10/12bit address mode,
 * and 0x20000 for 15bit mode.
 */
#define ADI_10BIT_SLAVE_ADDR_SIZE	SZ_4K
#define ADI_10BIT_SLAVE_OFFSET		0x8000
#define ADI_12BIT_SLAVE_ADDR_SIZE	SZ_16K
#define ADI_12BIT_SLAVE_OFFSET		0x8000
#define ADI_15BIT_SLAVE_ADDR_SIZE	SZ_128K
#define ADI_15BIT_SLAVE_OFFSET		0x20000

/* Timeout (ms) for the trylock of hardware spinlocks */
#define ADI_HWSPINLOCK_TIMEOUT		5000
/*
 * ADI controller has 50 channels including 2 software channels
 * and 48 hardware channels.
 */
#define ADI_HW_CHNS			50

#define ADI_FIFO_DRAIN_TIMEOUT		1000
#define ADI_READ_TIMEOUT		2000

/*
 * Read back address from REG_ADI_RD_DATA bit[30:16] which maps to:
 * REG_ADI_RD_CMD bit[14:0] for r2p0
 * REG_ADI_RD_CMD bit[16:2] for r3p0
 */
#define RDBACK_ADDR_MASK_R2		GENMASK(14, 0)
#define RDBACK_ADDR_MASK_R3		GENMASK(16, 2)
#define RDBACK_ADDR_SHIFT_R3		2

/* Registers definitions for PMIC watchdog controller */
#define REG_WDG_LOAD_LOW		0x0
#define REG_WDG_LOAD_HIGH		0x4
#define REG_WDG_CTRL			0x8
#define REG_WDG_LOCK			0x20

/* Bits definitions for register REG_WDG_CTRL */
#define BIT_WDG_RUN			BIT(1)
#define BIT_WDG_NEW			BIT(2)
#define BIT_WDG_RST			BIT(3)

/* Bits definitions for register REG_MODULE_EN */
#define BIT_WDG_EN			BIT(2)

/* Registers definitions for PMIC */
#define PMIC_RST_STATUS			0xee8
#define PMIC_MODULE_EN			0xc08
#define PMIC_CLK_EN			0xc18
#define PMIC_WDG_BASE			0x80

/* Definition of PMIC reset status register */
#define HWRST_STATUS_SECURITY		0x02
#define HWRST_STATUS_RECOVERY		0x20
#define HWRST_STATUS_NORMAL		0x40
#define HWRST_STATUS_ALARM		0x50
#define HWRST_STATUS_SLEEP		0x60
#define HWRST_STATUS_FASTBOOT		0x30
#define HWRST_STATUS_SPECIAL		0x70
#define HWRST_STATUS_PANIC		0x80
#define HWRST_STATUS_CFTREBOOT		0x90
#define HWRST_STATUS_AUTODLOADER	0xa0
#define HWRST_STATUS_IQMODE		0xb0
#define HWRST_STATUS_SPRDISK		0xc0
#define HWRST_STATUS_FACTORYTEST	0xe0
#define HWRST_STATUS_WATCHDOG		0xf0

/* Use default timeout 50 ms that converts to watchdog values */
#define WDG_LOAD_VAL			((50 * 32768) / 1000)
#define WDG_LOAD_MASK			GENMASK(15, 0)
#define WDG_UNLOCK_KEY			0xe551

struct sprd_adi_wdg {
	u32 base;
	u32 rst_sts;
	u32 wdg_en;
	u32 wdg_clk;
};

struct sprd_adi_data {
	u32 slave_offset;
	u32 slave_addr_size;
	int (*read_check)(u32 val, u32 reg);
};

struct sprd_adi {
	struct udevice		*dev;
	void __iomem		*base;
	struct hwspinlock	hwlock;
	bool			has_hwlock;
	unsigned long		slave_base;
	const struct sprd_adi_data *data;
};

static int sprd_adi_check_addr(struct sprd_adi *sadi, u32 reg)
{
	if (reg >= sadi->data->slave_addr_size) {
		dev_err(sadi->dev,
			"slave address offset is incorrect, reg = 0x%x\n",
			reg);
		return -EINVAL;
	}

	return 0;
}

static int sprd_adi_drain_fifo(struct sprd_adi *sadi)
{
	u32 timeout = ADI_FIFO_DRAIN_TIMEOUT;
	u32 sts;

	do {
		sts = readl_relaxed(sadi->base + REG_ADI_ARM_FIFO_STS);
		if (sts & BIT_FIFO_EMPTY)
			break;

		cpu_relax();
	} while (--timeout);

	if (timeout == 0) {
		dev_err(sadi->dev, "drain write fifo timeout\n");
		return -EBUSY;
	}

	return 0;
}

static int sprd_adi_fifo_is_full(struct sprd_adi *sadi)
{
	return readl_relaxed(sadi->base + REG_ADI_ARM_FIFO_STS) & BIT_FIFO_FULL;
}

static int sprd_adi_read_check(u32 val, u32 addr)
{
	u32 rd_addr;

	rd_addr = (val & RD_ADDR_MASK) >> RD_ADDR_SHIFT;

	if (rd_addr != addr) {
		pr_err("ADI read error, addr = 0x%x, val = 0x%x\n", addr, val);
		return -EIO;
	}

	return 0;
}

static int sprd_adi_read_check_r2(u32 val, u32 reg)
{
	return sprd_adi_read_check(val, reg & RDBACK_ADDR_MASK_R2);
}

static int sprd_adi_read_check_r3(u32 val, u32 reg)
{
	return sprd_adi_read_check(val, (reg & RDBACK_ADDR_MASK_R3) >> RDBACK_ADDR_SHIFT_R3);
}

static int sprd_adi_read(struct sprd_adi *sadi, u32 reg, u32 *read_val)
{
	int read_timeout = ADI_READ_TIMEOUT;
	u32 val;
	int ret = 0;

	if (sadi->has_hwlock) {
		ret = hwspinlock_lock_timeout(&sadi->hwlock, ADI_HWSPINLOCK_TIMEOUT);
		if (ret) {
			dev_err(sadi->dev, "get the hw lock failed\n");
			return ret;
		}
	}

	ret = sprd_adi_check_addr(sadi, reg);
	if (ret)
		goto out;

	/*
	 * Set the slave address offset need to read into RD_CMD register,
	 * then ADI controller will start to transfer automatically.
	 */
	writel_relaxed(reg, sadi->base + REG_ADI_RD_CMD);

	/*
	 * Wait read operation complete, the BIT_RD_CMD_BUSY will be set
	 * simultaneously when writing read command to register, and the
	 * BIT_RD_CMD_BUSY will be cleared after the read operation is
	 * completed.
	 */
	do {
		val = readl_relaxed(sadi->base + REG_ADI_RD_DATA);
		if (!(val & BIT_RD_CMD_BUSY))
			break;

		cpu_relax();
	} while (--read_timeout);

	if (read_timeout == 0) {
		dev_err(sadi->dev, "ADI read timeout\n");
		ret = -EBUSY;
		goto out;
	}

	/*
	 * The return value before adi r5p0 includes data and read register
	 * address, from bit 0to bit 15 are data, and from bit 16 to bit 30
	 * are read register address. Then we can check the returned register
	 * address to validate data.
	 */
	if (sadi->data->read_check) {
		ret = sadi->data->read_check(val, reg);
		if (ret < 0)
			goto out;
	}

	*read_val = val & RD_VALUE_MASK;

out:
	if (sadi->has_hwlock)
		hwspinlock_unlock(&sadi->hwlock);
	return ret;
}

static int sprd_adi_write(struct sprd_adi *sadi, u32 reg, u32 val)
{
	u32 timeout = ADI_FIFO_DRAIN_TIMEOUT;
	int ret;

	if (sadi->has_hwlock) {
		ret = hwspinlock_lock_timeout(&sadi->hwlock, ADI_HWSPINLOCK_TIMEOUT);
		if (ret) {
			dev_err(sadi->dev, "get the hw lock failed\n");
			return ret;
		}
	}

	ret = sprd_adi_check_addr(sadi, reg);
	if (ret)
		goto out;

	ret = sprd_adi_drain_fifo(sadi);
	if (ret < 0)
		goto out;

	/*
	 * we should wait for write fifo is empty before writing data to PMIC
	 * registers.
	 */
	do {
		if (!sprd_adi_fifo_is_full(sadi)) {
			writel_relaxed(val, (void __iomem *)(sadi->slave_base + reg));
			break;
		}

		cpu_relax();
	} while (--timeout);

	if (timeout == 0) {
		dev_err(sadi->dev, "write fifo is full\n");
		ret = -EBUSY;
	}

out:
	if (sadi->has_hwlock)
		hwspinlock_unlock(&sadi->hwlock);
	return ret;
}

static int sprd_adi_xfer(struct udevice *dev, unsigned int bitlen,
			 const void *dout, void *din, unsigned long flags)
{
	struct udevice *bus = dev->parent;
	struct sprd_adi *sadi = dev_get_priv(bus);
	u32 reg, val;
	int ret;

	if (din) {
		reg = *(u32 *)din;
		ret = sprd_adi_read(sadi, reg, &val);
		*(u32 *)din = val;
	} else if (dout) {
		u32 *p = (u32 *)dout;
		reg = *p++;
		val = *p;
		ret = sprd_adi_write(sadi, reg, val);
	} else {
		dev_err(sadi->dev, "no buffer for transfer\n");
		ret = -EINVAL;
	}

	return ret;
}

static void sprd_adi_hw_init(struct sprd_adi *sadi)
{
	ofnode node = dev_ofnode(sadi->dev);
	int i, size, chn_cnt;
	const __be32 *list;
	u32 tmp;

	/* Set all channels as default priority */
	writel_relaxed(0, sadi->base + REG_ADI_CHN_PRIL);
	writel_relaxed(0, sadi->base + REG_ADI_CHN_PRIH);

	/* Set clock auto gate mode */
	tmp = readl_relaxed(sadi->base + REG_ADI_GSSI_CFG0);
	tmp &= ~BIT_CLK_ALL_ON;
	writel_relaxed(tmp, sadi->base + REG_ADI_GSSI_CFG0);

	/* Set hardware channels setting */
	list = ofnode_get_property(node, "sprd,hw-channels", &size);
	if (!list || !size) {
		dev_info(sadi->dev, "no hw channels setting in node\n");
		return;
	}

	chn_cnt = size / 8;
	for (i = 0; i < chn_cnt; i++) {
		u32 value;
		u32 chn_id = be32_to_cpu(*list++);
		u32 chn_config = be32_to_cpu(*list++);

		/* Channel 0 and 1 are software channels */
		if (chn_id < 2)
			continue;

		writel_relaxed(chn_config, sadi->base +
			       REG_ADI_CHN_ADDR(chn_id));

		if (chn_id < 32) {
			value = readl_relaxed(sadi->base + REG_ADI_CHN_EN);
			value |= BIT(chn_id);
			writel_relaxed(value, sadi->base + REG_ADI_CHN_EN);
		} else if (chn_id < ADI_HW_CHNS) {
			value = readl_relaxed(sadi->base + REG_ADI_CHN_EN1);
			value |= BIT(chn_id - 32);
			writel_relaxed(value, sadi->base + REG_ADI_CHN_EN1);
		}
	}

	/* FIXME this code does not belong here */
	if (sprd_adi_read(sadi, 0x19b0, &tmp))
		dev_err(sadi->dev, "read error");
	dev_info(sadi->dev, "VDDSDCORE voltage register:    %08x\n", tmp);
	if (sprd_adi_read(sadi, 0x19ac, &tmp))
		dev_err(sadi->dev, "read error");
	dev_info(sadi->dev, "VDDSDCORE power down register: %08x\n", tmp);
	sprd_adi_write(sadi, 0x19ac, 0);

	if (sprd_adi_read(sadi, 0x19bc, &tmp))
		dev_err(sadi->dev, "read error");
	dev_info(sadi->dev, "VDDSDIO voltage register:    %08x\n", tmp);
	if (sprd_adi_read(sadi, 0x19b8, &tmp))
		dev_err(sadi->dev, "read error");
	dev_info(sadi->dev, "VDDSDIO power down register: %08x\n", tmp);
	sprd_adi_write(sadi, 0x19b8, 0);
}

static int sprd_adi_probe(struct udevice *dev)
{
	const struct sprd_adi_data *data;
	struct sprd_adi *sadi;
	int ret;

	data = (const struct sprd_adi_data *)dev_get_driver_data(dev);
	if (!data) {
		dev_err(dev, "no matching driver data found\n");
		return -EINVAL;
	}

	sadi = dev_get_priv(dev);

	sadi->base = dev_read_addr_ptr(dev);
	if (!sadi->base)
		return -EINVAL;

	sadi->slave_base = (unsigned long)sadi->base + data->slave_offset;
	sadi->dev = dev;
	sadi->data = data;
	ret = hwspinlock_get_by_index(dev, 0, &sadi->hwlock);
	if (!ret)
		sadi->has_hwlock = true;
	else
		dev_info(dev, "cannot get hwspinlock (%d)\n", ret);

	sprd_adi_hw_init(sadi);

	return 0;
}

static const struct dm_spi_ops sprd_adi_ops = {
	.xfer		= sprd_adi_xfer,
};

static struct sprd_adi_data sc9860_data = {
	.slave_offset = ADI_10BIT_SLAVE_OFFSET,
	.slave_addr_size = ADI_10BIT_SLAVE_ADDR_SIZE,
	.read_check = sprd_adi_read_check_r2,
};

static struct sprd_adi_data sc9863_data = {
	.slave_offset = ADI_12BIT_SLAVE_OFFSET,
	.slave_addr_size = ADI_12BIT_SLAVE_ADDR_SIZE,
	.read_check = sprd_adi_read_check_r3,
};

static struct sprd_adi_data ums512_data = {
	.slave_offset = ADI_15BIT_SLAVE_OFFSET,
	.slave_addr_size = ADI_15BIT_SLAVE_ADDR_SIZE,
	.read_check = sprd_adi_read_check_r3,
};

static const struct udevice_id sprd_adi_of_match[] = {
	{
		.compatible = "sprd,sc9860-adi",
		.data = (ulong)&sc9860_data,
	},
	{
		.compatible = "sprd,sc9863-adi",
		.data = (ulong)&sc9863_data,
	},
	{
		.compatible = "sprd,ums512-adi",
		.data = (ulong)&ums512_data,
	},
	{ },
};

U_BOOT_DRIVER(spi_sprd_adi) = {
	.name		= "spi-sprd-adi",
	.id		= UCLASS_SPI,
	.of_match	= sprd_adi_of_match,
	.ops		= &sprd_adi_ops,
	.priv_auto	= sizeof(struct sprd_adi),
	.probe		= sprd_adi_probe,
};
