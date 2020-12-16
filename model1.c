#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef unsigned long __uintptr_t;
#include "tree.h"
#include "models.h"

#define NAS5311	3
#define NSTEP	6
#define NENDSTOP	4

typedef struct _model {
	int		as5311_pos_lo[NAS5311];
	int		as5311_pos_hi[NAS5311];
	double		as5311_pos[NAS5311];
	int		sig_prev_data; 
	int		sig_first;
	int		sig_steppos[NSTEP];
	uint64_t	sig_last_step_change[NSTEP];
	uint64_t	sig_endstop_time_valid[NENDSTOP];
	int		mc_state;
	int		mc_len;
	int		mc_off;
	uint8_t		mc_buf[256];
} model_t;

#define SIG_ENDSTOP_CYCLES	2880
#define MAX_TIME ((uint64_t)(-1ll))

static void mod_signal(model_t *m, buffer_elem_t *be);
static void mod_as5311(model_t *m, buffer_elem_t *be);
static void mod_mcu(model_t *m, buffer_elem_t *be);
static void sig_tick(model_t *m, buffer_elem_t *be);
static int verbose = 0;

#define lD(...) if (verbose) printf(__VA_ARGS__)

void *
init_model1(int verb)
{
	model_t *m = calloc(sizeof(*m), 1);
	int i;

	verbose = verb;

	for (i = 0; i < NAS5311; ++i)
		m->as5311_pos_lo[i] = -1;
	for (i = 0; i < NENDSTOP; ++i)
		m->sig_endstop_time_valid[i] = MAX_TIME;

	m->sig_first = 1;

	return m;
}

void
model1(void *ctx, buffer_elem_t *be)
{
	model_t *m = ctx;

	switch (be->mtype) {
	case MT_MCU:
		mod_mcu(m, be);
		break;
	case MT_SYSTIME:
		break;
	case MT_SIG:
		mod_signal(m, be);
		break;
	case MT_AS5311:
		mod_as5311(m, be);
		break;
	case MT_DRO:
		break;
	}

	sig_tick(m, be);

	printf("model as[0]=%f as[1]=%f as[2]=%f\n",
		m->as5311_pos[0], m->as5311_pos[1], m->as5311_pos[2]);
}

static void
mod_signal(model_t *m, buffer_elem_t *be)
{
	uint32_t data = be->values[0].ui;
	int valid = be->values[1].ui;
	int i;
	uint32_t diff;

	if (valid == 0)
		return;

	if (m->sig_first) {
		m->sig_prev_data = data; 
		m->sig_first = 0;
	}
	/*
	 * [0] step1, stepper_z2
	 * [1] dir1
	 * [2] step2, stepper_z1
	 * [3] dir2
	 * [4] step3, stepper_z
	 * [5] dir3
	 * [6] step4, stepper_y
	 * [7] dir4
	 * [8] step5, stepper_x
	 * [9] dir5
	 * [10] step6, stepper_e
	 * [11] dir6
	 * [12] endstop1, stepper_x
	 * [13] endstop2, stepper_y
	 * [14] endstop3, stepper_z
	 * [15] endstop4, bltouch
	 * [16] pwm1, heater_extruder
	 * [17] pwm3, heater_bed
	 * [18] pwm11, bltouch
	 * [19] pwm5, part cooling fan
	 */
	diff = m->sig_prev_data ^ data;
	m->sig_prev_data = data;

	for (i = 0; i < 5; ++i) {
		int sbit = 1 << (2 * i);
		int dbit = 1 << (2 * i + 1);

		if (diff & dbit) {
			/*
			 * check setup / hold times. in case of dir it just means
			 * it must not change on the rising edge of step
			 */
			if ((diff & sbit) && (data & sbit))
				report("dir setup/hold time violation\n");
		}

		if (diff & sbit) {
			/* step must be at least 5 cycles hi/lo */
			if (be->systime - m->sig_last_step_change[i] < 5)
				report("step period violation\n");
		}

		if ((diff & sbit) && (data & sbit)) {
			int dir = !!(data & dbit);

			printf("%ld step on channel %d in dir %d\n", be->systime, i, dir);

			m->sig_last_step_change[i] = be->systime;
			m->sig_steppos[i] += dir ? 1 : -1;
		}
	}
	for (i = 0; i < 3; ++i) {
		int ebit = 1 << (i + 12);
		if (diff & ebit) {
			int state = !!(data & ebit);

			printf("%ld endstop %d changed state to %d\n", be->systime, i, state);
			if (state == 0)
				m->sig_endstop_time_valid[i] = be->systime + SIG_ENDSTOP_CYCLES;
			else
				m->sig_endstop_time_valid[i] = MAX_TIME;
		}
	}
}

static void
sig_tick(model_t *m, buffer_elem_t *be)
{
	int i;

	for (i = 0; i < NENDSTOP; ++i) {
		if (be->systime >= m->sig_endstop_time_valid[i]) {
			printf("%ld endstop %d valid\n", be->systime, i);
			m->sig_endstop_time_valid[i] = MAX_TIME;
		}
	}
}

static void
mod_as5311(model_t *m, buffer_elem_t *be)
{
	int ch = be->values[0].ui;
	char *type = be->values[1].s;
	int pos = be->values[2].ui;
	char *status = be->values[3].s;
	
	if (ch >= NAS5311)
		report("as5311 channel %d out of range\n", ch);

	if (strcmp(status, ".OCF.") != 0)
		report("invalid as5311 status %s on channel %d\n", status, ch);

	if (strcmp(type, "mag") == 0)
		return;

	lD("as5311[%d] type %s pos %d status %s\n", ch, type, pos, status);

	if (m->as5311_pos_lo[ch] == -1) {
		/* first time here */
		m->as5311_pos_lo[ch] = pos;
		m->as5311_pos_hi[ch] = 0;
	}

	/* detect over/underrun at 1/3, 2/3 */
	if (m->as5311_pos_lo[ch] > 2730 && pos < 1365)
		++m->as5311_pos_hi[ch];
	else if (m->as5311_pos_lo[ch] < 1365 && pos > 2730)
		--m->as5311_pos_hi[ch];

	/* 4096 steps per 2mm */
	m->as5311_pos_lo[ch] = pos;
	m->as5311_pos[ch] = (m->as5311_pos_hi[ch] * 4096 + m->as5311_pos_lo[ch]) / 2048.;
}

#define MS_IDLE		0
#define MS_IN_PACKET	1
#define MS_EOP		2

uint16_t
crc16_ccitt(uint8_t *buf, uint_fast8_t len)
{
	uint16_t crc = 0xffff;
	while (len--) {
		uint8_t data = *buf++;
		data ^= crc & 0xff;
		data ^= data << 4;
		crc = ((((uint16_t)data << 8) | (crc >> 8)) ^ (uint8_t)(data >> 4)
		       ^ ((uint16_t)data << 3));
	}
	return crc;
}

static void
mod_mcu(model_t *m, buffer_elem_t *be)
{
	int dir = strcmp(be->values[0].s, "tx");
	uint8_t data = be->values[1].ui;

	/* don't parse tx yet */
	if (dir == 0)
		return;

printf("%14ld rx %02x\n", be->systime, data);
	/* find frame */
	if (m->mc_state == MS_IDLE) {
		if (data == 0x7e) {
			/* ignore */
			return;
		}
		if (data < 3) {
#if 0
			report("data too small\n");
#else
			printf("data too small\n");
			data = 3;
#endif
		}
		m->mc_len = data - 2;
		m->mc_off = 0;
		m->mc_buf[m->mc_off++] = data;
		m->mc_state = MS_IN_PACKET;
	} else if (m->mc_state == MS_IN_PACKET) {
		m->mc_buf[m->mc_off++] = data;
		if (--m->mc_len == 0)
			m->mc_state = MS_EOP;
	} else if (m->mc_state == MS_EOP) {
		uint16_t crc;

		if (data != 0x7e)
#if 0
			report("%ld end of packet not found\n", be->systime);
#else
			printf("%ld end of packet not found\n", be->systime);
#endif
		/* check crc */
		crc = crc16_ccitt(m->mc_buf, m->mc_off - 2);
		if (crc != m->mc_buf[m->mc_off - 2] * 256 +
			m->mc_buf[m->mc_off - 1])
#if 0
			report("bad crc in mcu packet\n");
#else
			printf("%ld bad crc in mcu packet\n", be->systime);
#endif
#if 1
		printf("crc %04x buf %02x %02x\n", crc,
			m->mc_buf[m->mc_off - 2],
			m->mc_buf[m->mc_off - 1]);
#endif

		m->mc_state = MS_IDLE;
	}
}
