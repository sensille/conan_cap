#ifndef __MODELS_H__
#define __MODELS_H__

typedef enum {
	VT_UINT	= 1,
	VT_HEX,
	VT_FLOAT,
	VT_STRING
} vtype_t;

typedef struct _value {
	vtype_t		type;
	union {
		uint64_t	ui;
		double		f;
		char		*s;
	};
} value_t;

typedef struct _buffer_elem {
	RB_ENTRY(_buffer_elem)	rbnode;
	uint64_t		systime;
	int			mtype;
	int			n;
	value_t			*values;
} buffer_elem_t;

typedef enum {
	MT_MCU,
	MT_SYSTIME,
	MT_SIG,
	MT_AS5311,
	MT_DRO,
} msg_type_t;

void model1(void *ctx, buffer_elem_t *be);
void *init_model1(int verbose);
void model2(void *ctx, buffer_elem_t *be);
void *init_model2(int verbose);

void __attribute__ ((format (printf, 1, 2))) report(char *fmt, ...);

#endif
