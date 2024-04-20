/* SPDX-License-Identifier: GPL-2.0 */

#include "tek-poe.h"

#include <errno.h>
#include <string.h>
#include <libubox/ulog.h>

typedef int (*poe_reply_handler)(struct mcu_state *ctx, uint8_t *reply);

/* Careful with this; Only works for set_detection/disconnect_type commands. */
#define PORT_ID_ALL	0x7f
#define PORT_ID_WRONG_DIALECT		0x61

static struct mcu *hack_mcu;

static int poe_cmd_queue(uint8_t *cmd, int len)
{
	return mcu_queue_cmd(hack_mcu, cmd, len);
}

static int poet_cmd_4_port(uint8_t cmd_id, uint8_t port[4], uint8_t data[4])
{
	uint8_t cmd[] = { cmd_id, 0x00, port[0], data[0], port[1], data[1],
					port[2], data[2], port[3], data[3] };

	return poe_cmd_queue(cmd, sizeof(cmd));
}

/* 0x00 - Set port enable
 *	0: Disable
 *	1: Enable
 */
static int poe_cmd_port_enable(struct mcu *mcu, uint8_t port, uint8_t enable)
{
	uint8_t cmd[] = { 0x00, 0x00, port, enable };

	return poe_cmd_queue(cmd, sizeof(cmd));
}

static int poe_cmd_port_mapping_enable(bool enable)
{
	uint8_t cmd[] = { 0x02, 0x00, enable };

	return poe_cmd_queue(cmd, sizeof(cmd));
}

/* 0x10 - Set port detection type
 *	1: Legacy Capacitive Detection only
 *	2: IEEE 802.3af 4-Point Detection only (Default)
 *	3: IEEE 802.3af 4-Point followed by Legacy
 *	4: IEEE 802.3af 2-Point detection (Not Supported)
 *	5: IEEE 802.3af 2-Point followed by Legacy
 */
static int poe_cmd_port_detection_type(uint8_t port, uint8_t type)
{
	uint8_t cmd[] = { 0x10, 0x00, port, type };

	return poe_cmd_queue(cmd, sizeof(cmd));
}

/* 0x11 - Set port classification
 *	0: Disable
 *	1: Enable
 */
static int poe_cmd_port_classification(uint8_t port[4], uint8_t enable[4])
{
	return poet_cmd_4_port(0x11, port, enable);
}

/* 0x13 - Set port disconnect type
 *	0: none
 *	1: AC-disconnect
 *	2: DC-disconnect
 *	3: DC with delay
 */
static int poe_cmd_port_disconnect_type(uint8_t port, uint8_t type)
{
	uint8_t cmd[] = { 0x13, 0x00, port, type };

	return poe_cmd_queue(cmd, sizeof(cmd));
}

/* 0x15 - Set port power limit type
 *	0: None. Power limit is 16.2W if the connected device is “low power”,
 *	   or the set high power limit if the device is “high power”.
 *	1: Class based. The power limit for class 4 devices is determined by the high power limit.
 *	2: User defined
 */
static int poe_cmd_port_power_limit_type(uint8_t port[4], uint8_t limit[4])
{
	return poet_cmd_4_port(0x15, port, limit);
}

/* 0x16 - Set port power budget
 *	values in 0.2W increments
 */
static int poe_cmd_port_power_budget(uint8_t port, uint8_t budget)
{
	uint8_t cmd[] = { 0x16, 0x00, port, budget };

	return poe_cmd_queue(cmd, sizeof(cmd));
}

/* 0x17 - Set power management mode
 *	0: None (No Power Management mode) (Default in Semi-Auto mode)
 *	1: Static Power Management with Port Priority(Default in Automode)
 *	2: Dynamic Power Management with Port Priority
 *	3: Static Power Management without Port Priority
 *	4: Dynamic Power Management without Port Priority
 */
static int poe_cmd_power_mgmt_mode(uint8_t mode)
{
	uint8_t cmd[] = { 0x17, 0x00, mode };

	return poe_cmd_queue(cmd, sizeof(cmd));
}

/* 0x18 - Set global power budget */
static int poe_cmd_global_power_budget(uint8_t pse, float budget, float guard)
{
	uint8_t cmd[] = { 0x18, 0x00, pse, 0x00, 0x00, 0x00, 0x00 };

	write16_be(cmd + 3, budget * 10);
	write16_be(cmd + 5, guard * 10);

	return poe_cmd_queue(cmd, sizeof(cmd));
}

/* 0x1a - Set port priority
 *	0: Low
 *	1: Normal
 *	2: High
 *	3: Critical
 */
static int poe_set_port_priority(uint8_t port[4], uint8_t priority[4])
{
	return poet_cmd_4_port(0x1a, port, priority);
}

/* 0x1c - Set port power-up mode
 *	0: PoE
 *	1: legacy
 *	2: pre-PoE+
 *	3: PoE+
 */
static int poe_set_port_power_up_mode(uint8_t port[4], uint8_t mode[4])
{
	return poet_cmd_4_port(0x1c, port, mode);
}

/* 0x20 - Get system info */
static int poe_cmd_status(void)
{
	uint8_t cmd[] = { 0x20 };

	return poe_cmd_queue(cmd, sizeof(cmd));
}

static int poe_reply_status(struct mcu_state *ctx, uint8_t *reply)
{
	const char *mode[] = {
		"Semi-auto I2C",
		"Semi-auto UART",
		"Auto I2C",
		"Auto UART"
	};
	const char *mcu[] = {
		"ST Micro ST32F100 Microcontroller",
		"Nuvoton M05xx LAN Microcontroller",
		"ST Micro STF030C8 Microcontroller",
		"Nuvoton M058SAN Microcontroller",
		"Nuvoton NUC122 Microcontroller"
	};
	const char *status[] = {
		"Global Disable pin is de-asserted:No system reset from the previous query cmd:Configuration saved",
		"Global Disable pin is de-asserted:No system reset from the previous query cmd:Configuration Dirty",
		"Global Disable pin is de-asserted:System reseted:Configuration saved",
		"Global Disable pin is de-asserted:System reseted:Configuration Dirty",
		"Global Disable Pin is asserted:No system reset from the previous query cmd:Configuration saved",
		"Global Disable Pin is asserted:No system reset from the previous query cmd:Configuration Dirty",
		"Global Disable Pin is asserted:System reseted:Configuration saved",
		"Global Disable Pin is asserted:System reseted:Configuration Dirty"
	};

	ctx->sys_mode = GET_STR(reply[2], mode);
	ctx->num_detected_ports = reply[3];
	ctx->sys_version = reply[7];
	ctx->sys_mcu = GET_STR(reply[8], mcu);
	ctx->sys_status = GET_STR(reply[9], status);
	ctx->sys_ext_version = reply[10];

	return 0;
}

/* 0x23 - Get power statistics */
static int poe_cmd_power_stats(void)
{
	uint8_t cmd[] = { 0x23 };

	return poe_cmd_queue(cmd, sizeof(cmd));
}

static int poe_reply_power_stats(struct mcu_state *ctx, uint8_t *reply)
{
	ctx->power_consumption = read16_be(reply + 2) * 0.1;

	return 0;
}

/* 0x26 - Get extended port config */
static int poe_cmd_port_ext_config(uint8_t port)
{
	uint8_t cmd[] = { 0x26, 0x00, port };

	return poe_cmd_queue(cmd, sizeof(cmd));
}

static int poe_reply_port_ext_config(struct mcu_state *ctx, uint8_t *reply)
{
	const char *mode[] = {
		"PoE",
		"Legacy",
		"pre-PoE+",
		"PoE+"
	};

	ctx->ports[reply[2]].poe_mode = GET_STR(reply[3], mode);

	return 0;
}

/* 0x28 - Get all all port status */
static int poe_cmd_4_port_status(uint8_t p1, uint8_t p2, uint8_t p3, uint8_t p4)
{
	uint8_t cmd[] = { 0x28, 0x00, p1, 1, p2, 1, p3, 1, p4, 1 };

	return poe_cmd_queue(cmd, sizeof(cmd));
}

static int poe_reply_4_port_status(struct mcu_state *ctx, uint8_t *reply)
{
	int i, port, pstate;

	const char *status[] = {
		[0] = "Disabled",
		[1] = "Searching",
		[2] = "Delivering power",
		[4] = "Fault",
		[5] = "Other fault",
		[6] = "Requesting power",
	};

	for (i = 2; i < 11; i+=2) {
		port = reply[i];
		pstate = reply[i + 1];

		if (port == 0xff) {
			continue;
		} else if (port >= MAX_PORT) {
			ULOG_WARN("Invalid port status packet (port=%d)\n", port);
			return -1;
		}

		ctx->ports[port].status = GET_STR(pstate & 0xf, status);
	}

	return 0;
}

/* 0x30 - Get port power statistics */
static int poe_cmd_port_power_stats(uint8_t port)
{
	uint8_t cmd[] = { 0x30, 0x00, port };

	return poe_cmd_queue(cmd, sizeof(cmd));
}

static int poe_reply_port_power_stats(struct mcu_state *ctx, uint8_t *reply)
{
	int port_idx = reply[2];

	ctx->ports[port_idx].watt = read16_be(reply + 9) * 0.1;
	return 0;
}

static poe_reply_handler reply_handler[] = {
	[0x20] = poe_reply_status,
	[0x23] = poe_reply_power_stats,
	[0x26] = poe_reply_port_ext_config,
	[0x28] = poe_reply_4_port_status,
	[0x30] = poe_reply_port_power_stats,
};

static int poe_default_reply_handler(uint8_t *reply)
{
	int cmd = reply[0];
	int ret = reply[2];

	if (ret)
		ULOG_WARN("Command 0x%x replied with error 0x%x\n", cmd, ret);
	return 0;
}

static int poe_reply_consume(struct mcu_state *ctx, uint8_t *reply)
{
	if (reply[0] > ARRAY_SIZE(reply_handler)) {
		ULOG_DBG("bcm: received reply with bad command id\n");
		return -1;
	}

	if (reply_handler[reply[0]]) {
		return reply_handler[reply[0]](ctx, reply);
	} else {
		poe_default_reply_handler(reply);
	}

	return 0;
}

static int bcm_handle_reply(struct mcu_state *ctx, uint8_t *reply, size_t len)
{
	if (len != 12)
		return -EINVAL;

	return poe_reply_consume(ctx, reply);
}

static int poet_setup(const struct port_config *ports, size_t num_ports)
{
	uint8_t port_ids[4], priorities[4], powerup_mode[4], limit_type[4];
	uint8_t enable_all[4] = {1, 1, 1, 1};
	size_t i = 0, num_okay = 0;

	do {
		for ( ; i < num_ports; i++) {
			if (!ports[i].enable)
				continue;

			port_ids[num_okay] = i;
			priorities[num_okay] = ports[i].priority;
			powerup_mode[num_okay] = ports[i].power_up_mode;
			limit_type[num_okay] = (ports[i].power_budget) ? 2 : 1;

			if (++num_okay == 4)
				break;
		};

		memset(enable_all + num_okay, 0xff, 4 - num_okay);
		memset(port_ids + num_okay, 0xff, 4 - num_okay);
		memset(priorities + num_okay, 0xff, 4 - num_okay);
		memset(powerup_mode + num_okay, 0xff, 4 - num_okay);
		memset(limit_type + num_okay, 0xff, 4 - num_okay);

		poe_set_port_priority(port_ids, priorities);
		poe_set_port_power_up_mode(port_ids, powerup_mode);
		poe_cmd_port_classification(port_ids, enable_all);
		poe_cmd_port_power_limit_type(port_ids, limit_type);

		num_okay = 0;
	} while (++i < num_ports);

	return 0;
}

static int bcm_port_setup(struct mcu *mcu, const struct config *config)
{
	size_t i;

	poe_cmd_port_disconnect_type(PORT_ID_ALL, 2);
	poe_cmd_port_detection_type(PORT_ID_ALL, 3);

	for (i = 0; i < config->port_count; i++) {
		if (!config->ports[i].enable || !config->ports[i].power_budget)
			continue;

		poe_cmd_port_power_budget(i, config->ports[i].power_budget);
	}

	poet_setup(config->ports, config->port_count);

	for (i = 0; i < config->port_count; i++)
		poe_cmd_port_enable(mcu, i, !!config->ports[i].enable);

	return 0;
}

static void poe_set_power_budget(const struct config *config)
{
	unsigned int pse;

	for (pse = 0; pse < 8; pse++) {
		if (!(config->pse_id_set_budget_mask & (1 << pse)))
			continue;

		poe_cmd_global_power_budget(pse, config->budget,
					    config->budget_guard);
	}
}

static int bcm_initial_setup(struct mcu *mcu, const struct config *config)
{
	hack_mcu = mcu;

	poe_cmd_status();
	poe_cmd_power_mgmt_mode(2);
	poe_cmd_port_mapping_enable(false);
	poe_set_power_budget(config);

	bcm_port_setup(mcu, config);

	return 0;
}

static int bcm_poll(struct mcu *mcu, const struct config *config)
{
	size_t i;

	poe_cmd_power_stats();

	for (i = 0; i < config->port_count; i += 4)
		poe_cmd_4_port_status(i, i + 1, i + 2, i + 3);

	for (i = 0; i < config->port_count; i++) {
		poe_cmd_port_ext_config(i);
		poe_cmd_port_power_stats(i);
	}

	return 0;
}

const struct poe_dialect broadcom_dialect = {
	.init_async = bcm_initial_setup,
	.init_ports_async = bcm_port_setup,
	.poll_async = bcm_poll,
	.enable_port_async = poe_cmd_port_enable,
	.handle_reply = bcm_handle_reply,
};
