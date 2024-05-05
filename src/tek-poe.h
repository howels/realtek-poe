/* SPDX-License-Identifier: GPL-2.0+ */

#ifndef TEK_POE_H
#define TEK_POE_H

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <libubox/utils.h>

#define ULOG_DBG(fmt, ...) ulog(LOG_DEBUG, fmt, ## __VA_ARGS__)

#define GET_STR(a, b)	((a) < ARRAY_SIZE(b) ? (b)[a] : NULL)
#define MAX(a, b)	(((a) > (b)) ? (a) : (b))
#define MAX_PORT	24

/*
 * Order of commands doesn't matter. These are just an internal representation
 * that gets mapped to a wire command based on the dialect. Value of "0" is
 * reserve for "dialect does not implement command".
 *   MCU_ are global commands
 *   PORT_ are "port" commands
 */
enum poe_cmd {
	CMD_NONE = 0,
	MCU_SET_POWER_MGMT_MODE,
	MCU_SET_POWER_BUDGET,
	MCU_ENABLE_PORT_MAPPING,
	PORT_ENABLE,
	PORT_ENABLE_CLASSIFICATION,
	PORT_SET_DETECTION_TYPE,
	PORT_SET_PRIORITY,
	PORT_SET_POE_MODE,
	PORT_SET_DISCONNECT_TYPE,
	PORT_SET_POWER_LIMIT_TYPE,
	PORT_SET_POWER_LIMIT,

	MCU_GET_SYSTEM_INFO,
	MCU_GET_POWER_STATS,
	PORT_GET_EXT_CONFIG,
	PORT_GET_SHORT_STATUS,
	PORT_GET_POWER_STATS,
	CMD_MAX
};

enum poe_cmd_flags {
	CMD_IS_4PORT,
	CMD_HAS_ALL_PORT,
};

struct dialect_map {
	uint8_t wire_id;
	uint8_t flags;
};

static inline int rev_map(struct dialect_map map[0x100])
{
	unsigned int wire_id, rev_map_entry;
	int i;

	for (i = 0; i < CMD_MAX; i++) {
		wire_id = map[i].wire_id;
		rev_map_entry = wire_id + 0x80;

		if (rev_map_entry > 0x100)
			return -EINVAL;

		map[rev_map_entry].wire_id = i;
	}
}

static inline int lookup(const struct dialect_map map[0x100], enum poe_cmd cmd)
{
	if (cmd > CMD_MAX)
		return -EINVAL;

	return map[cmd].wire_id;
}

static inline int rev_lookup(const struct dialect_map map[0x100], uint8_t wire_id)
{
	unsigned int rev_map_entry = wire_id + 0x80;

	if (rev_map_entry > 0x100)
		return -EINVAL;

	return map[rev_map_entry].wire_id;
}

struct mcu;

struct port_state {
	const char *status;
	float watt;
	const char *poe_mode;
};

struct mcu_state {
	const char *sys_mode;
	uint8_t sys_version;
	const char *sys_mcu;
	const char *sys_status;
	uint8_t sys_ext_version;
	float power_consumption;
	unsigned int num_detected_ports;

	struct port_state ports[MAX_PORT];
};

struct port_config {
	char name[16];
	unsigned int valid : 1;
	unsigned int enable : 1;
	uint8_t priority;
	uint8_t power_up_mode;
	uint8_t power_budget;
};

struct config {
	float budget;
	float budget_guard;

	unsigned int port_count;
	uint8_t pse_id_set_budget_mask;
	struct port_config ports[MAX_PORT];
};

struct poe_dialect {
	int (*init_async)(struct mcu *mcu, const struct config *cfg);
	int (*init_ports_async)(struct mcu *mcu, const struct config *cfg);
	int (*enable_port_async)(struct mcu *mcu, uint8_t port, uint8_t enable);
	int (*poll_async)(struct mcu *mcu, const struct config *cfg);
	int (*reset)(struct mcu *mcu);
	int (*handle_reply)(struct mcu_state *ctx, uint8_t *reply, size_t len);
};

int mcu_queue_cmd(struct mcu *mcu, uint8_t *cmd_buf, size_t len);

static inline uint16_t read16_be(uint8_t *raw)
{
	return (uint16_t)raw[0] << 8 | raw[1];
}

static inline void write16_be(uint8_t *raw, uint16_t value)
{
	raw[0] = value >> 8;
	raw[1] =  value & 0xff;
}

extern const struct poe_dialect broadcom_dialect;
extern const struct poe_dialect realtek_dialect;

#endif /* TEK_POE_H */
