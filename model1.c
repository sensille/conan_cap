#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef unsigned long __uintptr_t;
#include "tree.h"
#include "models.h"

#define NAS5311		3
#define NSTEP		6
#define NENDSTOP	4

#define HZ 48000000

#define CHG_AS_X1	1
#define CHG_AS_X2	2
#define CHG_AS_Y	4
#define CHG_STEP	8

/*
 * 270mm z
 * 1/8 z 1.5mm 200
 * 1/32 x/y 32mm 400
 */
#define X_HOME	-12
#define Y_HOME	0
#define Z_HOME	270
#define Z_STEPS_PER_MM (200.0 * 8 / 1.5)
#if 0
#define XY_STEPS_PER_MM (400.0 * 32 / 32.0)
#else
#define XY_STEPS_PER_MM (1/0.0024921)
#endif

struct _model;
struct _mcu_ch;

#define MAX(x, y) ((x) > (y) ? (x) : (y))

typedef struct _mcu_ch {
	int		mc_state;
	int		mc_len;
	int		mc_off;
	uint8_t		mc_buf[256];
	void		(*packet)(struct _model *m, struct _mcu_ch *mc, buffer_elem_t *be);
	int		mc_next_seq;
} mcu_ch_t;

typedef struct _model {
	struct _parser	*parser;
	int		changed;
	int		as5311_pos_lo[NAS5311];
	int		as5311_pos_hi[NAS5311];
	double		as5311_pos[NAS5311];
	uint32_t	sig_prev_data;
	int		sig_first;
	int		sig_steppos[NSTEP];
	uint64_t	sig_last_step_change[NSTEP];
	mcu_ch_t	mcu_ch[2];
	uint64_t	endstop_last_change[NENDSTOP];
	uint64_t	endstop_arm[NENDSTOP];
	int		endstop_sample_count[NENDSTOP];
	int		endstop_pin_value[NENDSTOP];
	uint64_t	endstop_time_valid[NENDSTOP];
	int		endstop_state[NENDSTOP];
	int		step_disable[NENDSTOP];		/* during homing */
	uint32_t	stepper_endstops[NSTEP];
	int		home_x_stepper_x;
	int		home_x_stepper_y;
	int		home_y_stepper_x;
	int		home_y_stepper_y;
	int		home_z[3];
	double		home_as[3];
	uint64_t	first_systime;
	int		chg_mask;
	double		chg_x;
	double		chg_y;
	uint64_t	chg_t;
} model_t;

#define MAX_TIME ((uint64_t)(-1ll))

static void mod_signal(model_t *m, buffer_elem_t *be);
static void mod_as5311(model_t *m, buffer_elem_t *be);
static void mod_mcu(model_t *m, buffer_elem_t *be);
static void sig_tick(model_t *m, buffer_elem_t *be);
static int verbose = 0;
static void mod_packet_rx(model_t *m, mcu_ch_t *mc, buffer_elem_t *be);
static void mod_packet_tx(model_t *m, mcu_ch_t *mc, buffer_elem_t *be);

#define lD(...) if (verbose) printf(__VA_ARGS__)

void *
init_model1(struct _parser *p, int verb)
{
	model_t *m = calloc(sizeof(*m), 1);
	int i;

	verbose = verb;
	m->parser = p;

	for (i = 0; i < NAS5311; ++i)
		m->as5311_pos_lo[i] = -1;

	m->sig_first = 1;

	m->mcu_ch[0].packet = mod_packet_tx;
	m->mcu_ch[1].packet = mod_packet_rx;
	m->mcu_ch[0].mc_next_seq = -1;
	m->mcu_ch[1].mc_next_seq = -1;

	/* which stepper reacts on which endstops */
	m->stepper_endstops[0] = 12; /* Z: endstop3, bltouch */
	m->stepper_endstops[1] = 12;
	m->stepper_endstops[2] = 12;
	m->stepper_endstops[3] = 0; /* E: none */
	m->stepper_endstops[4] = 3; /* Y: endstop2 */
	m->stepper_endstops[5] = 3; /* X: endstop1 */

	return m;
}

void
model1(void *ctx, buffer_elem_t *be)
{
	model_t *m = ctx;

	m->changed = 0;

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

	if (!m->changed)
		return;

	double z1 = (m->sig_steppos[0] - m->home_z[0]) / Z_STEPS_PER_MM + Z_HOME;
	double z2 = (m->sig_steppos[1] - m->home_z[1]) / Z_STEPS_PER_MM + Z_HOME;
	double z3 = (m->sig_steppos[2] - m->home_z[2]) / Z_STEPS_PER_MM + Z_HOME;
	int a = m->sig_steppos[4];	/* stepper_x */
	int b = -m->sig_steppos[3];	/* stepper_y */
	int home_x = m->home_x_stepper_x + (-m->home_x_stepper_y);
	int home_y = m->home_y_stepper_x - (-m->home_y_stepper_y);
	double x = ((a + b - home_x) / 2.) / XY_STEPS_PER_MM + X_HOME;
	double y = ((a - b - home_y) / 2.) / XY_STEPS_PER_MM + Y_HOME;
	double as_x1 = -(m->as5311_pos[0] - m->home_as[0]) + X_HOME;
	double as_x2 = +(m->as5311_pos[1] - m->home_as[1]) + X_HOME;
	double as_y = m->as5311_pos[2] - m->home_as[2] + Y_HOME;
	int e = m->sig_steppos[5];

	if (m->first_systime == 0)
		m->first_systime = be->systime;

	lD("% 14ld model as[0]=%f as[1]=%f as[2]=%f x=%f y=%f z1=%f z2=%f z3=%f e=%d\n",
		be->systime, as_x1, as_x2, as_y, x, y, z1, z2, z3, e);
#if 0
	printf("% 8.9f DATA chg %d as %f %f %f x/y %f %f z %f %f %f e %d delta_as %f dx_as1 %f dx_as2 %f dy_as %f\n",
		(double)(be->systime - m->first_systime) / HZ,
		m->changed,
		as_x1, as_x2, as_y, x, y, z1, z2, z3, e,
		as_x1 - as_x2, x - as_x1, x - as_x2, y - as_y);
#endif
	m->chg_mask |= m->changed;
	if ((m->chg_mask & 7) == 7) {
		/* all as5311 seen, emit one line */
		printf("% 8.9f CMPL as %f %f %f x/y %f %f z %f %f %f e %d\n",
			(double)((be->systime + m->chg_t) / 2 - m->first_systime) / HZ,
			as_x1, as_x2, as_y,
			(x + m->chg_x) / 2, (y + m->chg_y) / 2,
			z1, z2, z3, e);
		m->chg_x = x;
		m->chg_y = y;
		m->chg_t = be->systime;
		m->chg_mask = 0;
	}
}

static void
mod_signal(model_t *m, buffer_elem_t *be)
{
	uint32_t data = be->values[0].ui;
	int valid = be->values[1].ui;
	int i;
	int j;
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
			int disable = 0;

			for (j = 0; j < NENDSTOP; ++j)
				if ((m->stepper_endstops[i] & (1 << j)) && m->step_disable[j])
					disable = 1;

			lD("% 14ld step on channel %d in dir %d disable %d\n",
				be->systime, i, dir, disable);

			if (disable)
				report("step after endstop triggered\n");

			m->sig_last_step_change[i] = be->systime;
			m->sig_steppos[i] += dir ? 1 : -1;
			m->changed |= CHG_STEP;
		}
	}
	for (i = 0; i < NENDSTOP; ++i) {
		int ebit = 1 << (i + 12);
		if (diff & ebit) {
			int state = !!(data & ebit);

			lD("% 14ld endstop %d changed state to %d\n", be->systime, i, state);
			m->endstop_last_change[i] = be->systime;
			m->endstop_state[i] = state;
		}
	}
}

static void
sig_tick(model_t *m, buffer_elem_t *be)
{
	int i;
	uint64_t start;

	for (i = 0; i < NENDSTOP; ++i) {
		if (m->endstop_arm[i] == 0)
			continue;

		if (m->endstop_pin_value[i] != m->endstop_state[i])
			continue;

		start = MAX(m->endstop_last_change[i], m->endstop_arm[i]);

		if (be->systime + be->n > start + m->endstop_sample_count[i]) {
			lD("% 14ld endstop %d got valid at %ld\n", be->systime, i,
				start + m->endstop_sample_count[i]);
			m->endstop_time_valid[i] = start + m->endstop_sample_count[i];
			m->endstop_arm[i] = 0;
			m->endstop_sample_count[i] = 0;
			m->step_disable[i] = 1;
			/* XXX hard code for now */
			printf("% 8.9f HOME endstop %d\n",
				(double)(be->systime - m->first_systime) / HZ, i);
			if (i == 0) {
				m->home_x_stepper_x = m->sig_steppos[4];
				m->home_x_stepper_y = m->sig_steppos[3];
				m->home_as[0] = m->as5311_pos[0];
				m->home_as[1] = m->as5311_pos[1];
			} else if (i == 1) {
				m->home_y_stepper_x = m->sig_steppos[4];
				m->home_y_stepper_y = m->sig_steppos[3];
				m->home_as[2] = m->as5311_pos[2];
			} else if (i == 2) {
				m->home_z[0] = m->sig_steppos[0];
				m->home_z[1] = m->sig_steppos[1];
				m->home_z[2] = m->sig_steppos[2];
			}
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

	if (ch == 0)
		m->changed |= CHG_AS_X1;
	else if (ch == 1)
		m->changed |= CHG_AS_X2;
	else if (ch == 2)
		m->changed |= CHG_AS_Y;
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

static uint8_t
read_byte(uint8_t **b, int *len)
{
	if (*len == 0)
		report("read_byte: end of packet reached\n");
	--*len;

	return *(*b)++;
}

static uint32_t
parse_arg(uint8_t **b, int *len)
{
    uint8_t c = read_byte(b, len);
    uint32_t v = c & 0x7f;

    if ((c & 0x60) == 0x60)
        v |= -0x20;

    while (c & 0x80) {
        c = read_byte(b, len);
        v = (v<<7) | (c & 0x7f);
    }

    return v;
}

#define CMD_GET_VERSION         0
#define CMD_SYNC_TIME           1
#define CMD_GET_TIME            2
#define CMD_CONFIG_PWM          3
#define CMD_SCHEDULE_PWM        4
#define CMD_CONFIG_STEPPER      5
#define CMD_QUEUE_STEP          6
#define CMD_SET_NEXT_STEP_DIR   7
#define CMD_RESET_STEP_CLOCK    8
#define CMD_STEPPER_GET_POS     9
#define CMD_ENDSTOP_SET_STEPPER 10
#define CMD_ENDSTOP_QUERY       11
#define CMD_ENDSTOP_HOME        12
#define CMD_TMCUART_WRITE       13
#define CMD_TMCUART_READ        14
#define CMD_SET_DIGITAL_OUT     15
#define CMD_CONFIG_DIGITAL_OUT  16
#define CMD_SCHEDULE_DIGITAL_OUT 17
#define CMD_UPDATE_DIGITAL_OUT  18
#define CMD_SHUTDOWN            19
#define CMD_STEPPER_GET_NEXT    20
#define CMD_CONFIG_DRO          21
#define CMD_CONFIG_AS5311       22
#define CMD_SD_QUEUE            23
#define CMD_CONFIG_ETHER        24
#define CMD_ETHER_MD_READ       25
#define CMD_ETHER_MD_WRITE      26
#define CMD_ETHER_SET_STATE     27
#define CMD_CONFIG_SIGNAL       28

static void
mod_packet_rx(model_t *m, mcu_ch_t *mc, buffer_elem_t *be)
{
	uint8_t *b = mc->mc_buf + 2;
	int len = mc->mc_off - 4;
	
	uint32_t type = parse_arg(&b, &len);

	lD("% 14ld ", be->systime);

	switch (type) {
	case CMD_GET_VERSION:
		lD("CMD_GET_VERSION\n");
		break;
	case CMD_SYNC_TIME:
		lD("CMD_SYNC_TIME\n");
		break;
	case CMD_GET_TIME:
		lD("CMD_GET_TIME\n");
		break;
	case CMD_CONFIG_PWM:
		lD("CMD_CONFIG_PWM\n");
		break;
	case CMD_SCHEDULE_PWM: {
		int channel = parse_arg(&b, &len);
		uint32_t clock = parse_arg(&b, &len);
		uint32_t on_ticks = parse_arg(&b, &len);
		uint32_t off_ticks = parse_arg(&b, &len);

		lD("CMD_SCHEDULE_PWM channel=%d clock=%u on_ticks=%u off_ticks=%u\n",
			channel, clock, on_ticks, off_ticks);
		break;
	}
	case CMD_CONFIG_STEPPER:
		lD("CMD_CONFIG_STEPPER\n");
		break;
	case CMD_QUEUE_STEP: {
		int channel = parse_arg(&b, &len);
		uint32_t interval = parse_arg(&b, &len);
		int32_t count = parse_arg(&b, &len);
		uint32_t add = parse_arg(&b, &len);

		lD("CMD_QUEUE_STEP channel=%d interval=%d count=%d add=%d\n",
			channel, interval, count, add);
		break;
	}
	case CMD_SET_NEXT_STEP_DIR: {
		int channel = parse_arg(&b, &len);
		uint32_t dir = parse_arg(&b, &len);

		lD("CMD_SET_NEXT_STEP_DIR channel=%d dir=%d\n",
			channel, dir);
		break;
	}
	case CMD_RESET_STEP_CLOCK: {
		int channel = parse_arg(&b, &len);
		uint32_t clock = parse_arg(&b, &len);

		lD("CMD_RESET_STEP_CLOCK channel=%d clock=%u\n",
			channel, clock);
		break;
	}
	case CMD_STEPPER_GET_POS: {
		int channel = parse_arg(&b, &len);
		lD("CMD_STEPPER_GET_POS channel=%d\n", channel);
		break;
	}
	case CMD_ENDSTOP_SET_STEPPER:
		lD("CMD_ENDSTOP_SET_STEPPER\n");
		break;
	case CMD_ENDSTOP_QUERY: {
		int channel = parse_arg(&b, &len);
		lD("CMD_ENDSTOP_QUERY channel=%d\n", channel);
		break;
	}
	case CMD_ENDSTOP_HOME: {
		int channel = parse_arg(&b, &len);
		uint32_t clock = parse_arg(&b, &len);
		uint32_t sample_count = parse_arg(&b, &len);
		uint32_t pin_value = parse_arg(&b, &len);
		uint64_t systime = clock ? relate_systime(m->parser, clock, 32) : 0;

		lD("CMD_ENDSTOP_HOME channel=%d clock=%u (%ld) sample_count=%d pin_value=%d\n",
			channel, clock, systime, sample_count, pin_value);

		if (sample_count == 0) {
			m->endstop_arm[channel] = 0;
			m->endstop_sample_count[channel] = 0;
			m->step_disable[channel] = 0;
		} else {
			m->endstop_arm[channel] = systime;
			m->endstop_sample_count[channel] = sample_count;
		}
		m->endstop_pin_value[channel] = pin_value;

		break;
	}
	case CMD_TMCUART_WRITE:
		lD("CMD_TMCUART_WRITE\n");
		break;
	case CMD_TMCUART_READ:
		lD("CMD_TMCUART_READ\n");
		break;
	case CMD_SET_DIGITAL_OUT:
		lD("CMD_SET_DIGITAL_OUT\n");
		break;
	case CMD_CONFIG_DIGITAL_OUT:
		lD("CMD_CONFIG_DIGITAL_OUT\n");
		break;
	case CMD_SCHEDULE_DIGITAL_OUT:
		lD("CMD_SCHEDULE_DIGITAL_OUT\n");
		break;
	case CMD_UPDATE_DIGITAL_OUT:
		lD("CMD_UPDATE_DIGITAL_OUT\n");
		break;
	case CMD_SHUTDOWN:
		lD("CMD_SHUTDOWN\n");
		break;
	case CMD_STEPPER_GET_NEXT:
		lD("CMD_STEPPER_GET_NEXT\n");
		break;
	case CMD_CONFIG_DRO:
		lD("CMD_CONFIG_DRO\n");
		break;
	case CMD_CONFIG_AS5311:
		lD("CMD_CONFIG_AS5311\n");
		break;
	case CMD_SD_QUEUE:
		lD("CMD_SD_QUEUE\n");
		break;
	case CMD_CONFIG_ETHER:
		lD("CMD_CONFIG_ETHER\n");
		break;
	case CMD_ETHER_MD_READ:
		lD("CMD_ETHER_MD_READ\n");
		break;
	case CMD_ETHER_MD_WRITE:
		lD("CMD_ETHER_MD_WRITE\n");
		break;
	case CMD_ETHER_SET_STATE:
		lD("CMD_ETHER_SET_STATE\n");
		break;
	case CMD_CONFIG_SIGNAL:
		lD("CMD_CONFIG_SIGNAL\n");
		break;
	default:
		lD("unkown command\n");
		break;
	}
}

#define RSP_GET_VERSION         0
#define RSP_GET_TIME            1
#define RSP_STEPPER_GET_POS     2
#define RSP_ENDSTOP_STATE       3
#define RSP_TMCUART_READ        4
#define RSP_SHUTDOWN            5
#define RSP_STEPPER_GET_NEXT    6
#define RSP_DRO_DATA            7
#define RSP_AS5311_DATA         8
#define RSP_SD_CMDQ             9
#define RSP_SD_DATQ            10
#define RSP_ETHER_MD_READ      11

static void
mod_packet_tx(model_t *m, mcu_ch_t *mc, buffer_elem_t *be)
{
	uint8_t *b = mc->mc_buf + 2;
	int len = mc->mc_off - 4;
	
	uint32_t type = parse_arg(&b, &len);

	lD("% 14ld ", be->systime);

	switch (type) {
	case RSP_GET_VERSION:
		lD("RSP_GET_VERSION\n");
		break;
	case RSP_GET_TIME:
		lD("RSP_GET_TIME\n");
		break;
	case RSP_STEPPER_GET_POS: {
		int channel = parse_arg(&b, &len);
		int32_t pos = parse_arg(&b, &len);

		lD("RSP_STEPPER_GET_POS channel=%d pos=%d\n", channel, pos );
		break;
	}
	case RSP_ENDSTOP_STATE: {
		int channel = parse_arg(&b, &len);
		uint32_t homing = parse_arg(&b, &len);
		uint32_t pin_value = parse_arg(&b, &len);
		lD("RSP_ENDSTOP_STATE channel=%d homing=%d pin_value=%d\n",
			channel, homing, pin_value);
		break;
	}
	case RSP_TMCUART_READ:
		lD("RSP_TMCUART_READ\n");
		break;
	case RSP_SHUTDOWN:
		lD("RSP_SHUTDOWN\n");
		break;
	case RSP_STEPPER_GET_NEXT:
		lD("RSP_STEPPER_GET_NEXT\n");
		break;
	case RSP_DRO_DATA:
		lD("RSP_DRO_DATA\n");
		break;
	case RSP_AS5311_DATA:
		lD("RSP_AS5311_DATA\n");
		break;
	case RSP_SD_CMDQ:
		lD("RSP_SD_CMDQ\n");
		break;
	case RSP_SD_DATQ:
		lD("RSP_SD_DATQ\n");
		break;
	case RSP_ETHER_MD_READ:
		lD("RSP_ETHER_MD_READ\n");
		break;
	}
}

static void
mod_mcu_ch(model_t *m, mcu_ch_t *mc, buffer_elem_t *be)
{
	uint8_t data = be->values[1].ui;

	/* find frame */
	if (mc->mc_state == MS_IDLE) {
		if (data == 0x7e) {
			/* ignore */
			return;
		}
		if (data < 3)
			report("data too small\n");
		mc->mc_len = data - 2;
		mc->mc_off = 0;
		mc->mc_buf[mc->mc_off++] = data;
		mc->mc_state = MS_IN_PACKET;
	} else if (mc->mc_state == MS_IN_PACKET) {
		mc->mc_buf[mc->mc_off++] = data;
		if (--mc->mc_len == 0)
			mc->mc_state = MS_EOP;
	} else if (mc->mc_state == MS_EOP) {
		uint16_t crc;

		if (data != 0x7e)
			report("%ld end of packet not found\n", be->systime);

		/* check crc */
		crc = crc16_ccitt(mc->mc_buf, mc->mc_off - 2);
		if (crc != mc->mc_buf[mc->mc_off - 2] * 256 +
			mc->mc_buf[mc->mc_off - 1])
			report("bad crc in mcu packet\n");

		/* check seq */
		if ((mc->mc_buf[1] & 0xf0) != 0x10)
			report("invalid seq marker in packet\n");

		if (mc->mc_next_seq == -1)
			mc->mc_next_seq = mc->mc_buf[1] & 0x0f;

		if ((mc->mc_buf[1] & 0x0f) != mc->mc_next_seq)
			report("invalid seq in packet\n");

		mc->mc_next_seq = (mc->mc_next_seq + 1) & 0x0f;

		mc->packet(m, mc, be);

		mc->mc_state = MS_IDLE;
	}
}

static void
mod_mcu(model_t *m, buffer_elem_t *be)
{
	int dir = !!strcmp(be->values[0].s, "tx");

	mod_mcu_ch(m, &m->mcu_ch[dir], be);
}
