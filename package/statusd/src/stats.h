/* SPDX-License-Identifier: GPL-2.0 */
#ifndef STATS_H
#define STATS_H

#include <stdint.h>

#define HIST 120
#define MAXCPU 8

struct netif {
	const char *name;
	int present, up;
	char ip[16];
	long speed;
	uint64_t rx, tx;
	double rxr, txr;
	float rxh[HIST], txh[HIST];
};

struct stats {
	int head, filled;

	int ncpu;
	double cpu, core[MAXCPU];
	float cpuh[HIST];
	int mhz, mhz_max;
	double load[3];

	long mem_total, mem_avail, mem_cached, cma_total, cma_free;
	uint64_t root_used;
	float memh[HIST];

	int ntemp;
	struct { char name[12]; double c; } temp[8];
	float temph[HIST];

	struct netif eth, wlan;
	char ssid[33];
	int rssi, linkspeed, freq;

	int ncard;
	struct { char id[16], name[40], state[16], fmt[16], owner[16], last[48]; int rate, ch; long last_t; } card[4];

	int bt_present, bt_on;

	int nmnt;
	struct { char dir[40], fs[10]; uint64_t size, used; } mnt[8];

	int nusb;
	struct { char name[40]; int mbps; } usb[8];

	char host[64], kernel[32];
	long uptime;
	int ntp_sync;
};

void stats_init(struct stats *s);
void stats_update(struct stats *s);
void stats_close(void);

#endif
