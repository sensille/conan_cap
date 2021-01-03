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
	MT_ABZ,
	MT_AS5311,
	MT_DRO,
} msg_type_t;

struct _parser;
void model1(void *ctx, buffer_elem_t *be);
void *init_model1(struct _parser *p,int verbose);
void model2(void *ctx, buffer_elem_t *be);
void *init_model2(struct _parser *p,int verbose);

void __attribute__ ((format (printf, 1, 2))) report(char *fmt, ...);
uint64_t relate_systime(struct _parser *p, uint64_t systime, int bits);

void enable_gcode(char *filename);
void gcode(buffer_elem_t *be, double time,
	double as_x1, double as_x2, double as_y,
	double x, double y, double z1, double z2, double z3,
	double e, double dr0);

#endif
