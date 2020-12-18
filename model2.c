#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef unsigned long __uintptr_t;
#include "tree.h"
#include "models.h"

#define NAS5311	3

typedef struct _model {
	struct _parser	*parser;
	int		sig_prev_data;
	int		sig_first;
} model_t;

static void mod_signal(model_t *m, buffer_elem_t *be);
static void sig_tick(model_t *m, buffer_elem_t *be);
static int verbose = 0;

#define lD(...) if (verbose) printf(__VA_ARGS__)

void *
init_model2(struct _parser *p, int verb)
{
	model_t *m = calloc(sizeof(*m), 1);
	m->parser = p;

	verbose = verb;

	m->sig_first = 1;

	return m;
}

void
model2(void *ctx, buffer_elem_t *be)
{
	model_t *m = ctx;

	switch (be->mtype) {
	case MT_MCU:
	case MT_SYSTIME:
	case MT_AS5311:
		break;
	case MT_SIG:
		mod_signal(m, be);
		break;
	}

	sig_tick(m, be);
}

static void
mod_signal(model_t *m, buffer_elem_t *be)
{
	uint32_t data = be->values[0].ui;
	int valid = be->values[1].ui;
	int i;

	if (valid == 0)
		return;

	if (m->sig_first) {
		m->sig_prev_data = data;
		m->sig_first = 0;
	}
	/*
	assign signal[4:0] = daq_valid;
	assign signal[9:5] = daq_end;
	assign signal[14:10] = daq_req;
	assign signal[19:15] = daq_grant;
	*/

	m->sig_prev_data = data;

	printf("%ld signal is ", be->systime);
	for (i = 19; i >= 0; --i) {
		if (i == 19) printf("grant=");
		if (i == 18) printf(" req=");
		if (i == 17) printf(" channel=");
		if (i == 15) printf(" pending=");
		if (i == 13) printf(" ack=");
		if (i == 11) printf(" valid=");
		if (i == 9) printf(" as_state=");
		if (i == 6) printf(" ");
		if (i == 3) printf(" state=");
		printf("%d", !!(data & (1 << i)));
	}
	printf(" cnt %ld\n", be->values[2].ui);
}

static void
sig_tick(model_t *m, buffer_elem_t *be)
{
}
