/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Copyright (C) 2022 Google, LLC
 *
 * A fixed rate clock referenced with one (ignored) clock specifier cell,
 * for the "lwext4,fixed-clock-cell" devicetree nodes of this example (see
 * dts/bindings/lwext4,fixed-clock-cell.yaml and
 * boards/qemu_cortex_m3.overlay).
 *
 * This file is derived from Zephyr's
 * drivers/clock_control/clock_control_fixed_rate.c and keeps its licence,
 * Apache-2.0 (the rest of this example is BSD-3-Clause). Only the
 * devicetree compatible differs.
 *
 * Why it exists: Zephyr's arm,pl022 SPI driver (the SSI controller of the
 * LM3S6965) reads its input clock rate through the clock control API and
 * takes the clock specifier's clk_id cell; without a clock it cannot set
 * any SPI clock rate. Zephyr's own "fixed-clock" provider has no cells, so
 * it cannot be used there.
 */

#include <zephyr/device.h>
#include <zephyr/drivers/clock_control.h>

#define DT_DRV_COMPAT lwext4_fixed_clock_cell

struct fixed_clock_cell_config {
	uint32_t rate;
};

static int fixed_clock_cell_on(const struct device *dev,
			       clock_control_subsys_t sys)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(sys);

	return 0;
}

static int fixed_clock_cell_off(const struct device *dev,
				clock_control_subsys_t sys)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(sys);

	return 0;
}

static enum clock_control_status
fixed_clock_cell_get_status(const struct device *dev,
			    clock_control_subsys_t sys)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(sys);

	return CLOCK_CONTROL_STATUS_ON;
}

static int fixed_clock_cell_get_rate(const struct device *dev,
				     clock_control_subsys_t sys,
				     uint32_t *rate)
{
	const struct fixed_clock_cell_config *config = dev->config;

	ARG_UNUSED(sys);

	*rate = config->rate;
	return 0;
}

static DEVICE_API(clock_control, fixed_clock_cell_api) = {
	.on = fixed_clock_cell_on,
	.off = fixed_clock_cell_off,
	.get_status = fixed_clock_cell_get_status,
	.get_rate = fixed_clock_cell_get_rate,
};

#define FIXED_CLOCK_CELL_INIT(idx)                                             \
	static const struct fixed_clock_cell_config                            \
		fixed_clock_cell_config_##idx = {                              \
			.rate = DT_INST_PROP(idx, clock_frequency),            \
	};                                                                     \
	DEVICE_DT_INST_DEFINE(idx, NULL, NULL, NULL,                           \
			      &fixed_clock_cell_config_##idx, PRE_KERNEL_1,    \
			      CONFIG_CLOCK_CONTROL_INIT_PRIORITY,              \
			      &fixed_clock_cell_api);

DT_INST_FOREACH_STATUS_OKAY(FIXED_CLOCK_CELL_INIT)
