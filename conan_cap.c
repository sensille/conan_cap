#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <linux/if_packet.h>
#include <net/ethernet.h> /* the L2 protocols */
#include <assert.h>

typedef unsigned long __uintptr_t;
#include "tree.h"
#include "models.h"

#define HZ 48000000
#define RETENTION 300	/* 300ms buffer for sorting */
#define RETENTION_CYCLES ((HZ / 1000) * RETENTION)

#define DAQT_MCU_RX           8
#define DAQT_MCU_RX_LONG      9
#define DAQT_MCU_TX           10
#define DAQT_MCU_TX_LONG      11
#define DAQT_AS5311_DAT       16
#define DAQT_AS5311_MAG       17
#define DAQT_SYSTIME_SET      32
#define DAQT_SYSTIME_ROLLOVER 33
#define DAQT_DRO_DATA         48
#define DAQT_SIGNAL_DATA      64

#define lD(...) if (verbose) printf(__VA_ARGS__)
#define SS_IDLE		0
#define SS_SAMPLE	1
#define SS_CNT_PRE	2
#define SS_CNT_8	3
#define SS_CNT_RLE	4
#define SS_CNT_DONE	5

typedef struct _pipeline {
	uint32_t	data;
	int		valid;
} pipeline_t;

typedef struct _signal {
	int		state;
	int		width;
	int		rle;
	int		slot;
	int		cnt;
	pipeline_t	pipeline[7];
	int		buflen;
	uint64_t	buf;
	int		first_packet;
	uint64_t	systime;
	int		systime_related;
} signal_t;

typedef struct _parser {
	uint16_t	seq;
	int		state;
	int		first_frame;
	uint64_t	systime_base;
	int		base_set;
	signal_t	signal;
	uint64_t	mcu_systime;
	int		mcu_systime_set;
	uint64_t	newest_systime;
	uint64_t	newest_delivered;
} parser_t;

#define MAX_VALUES	20

const char *type_names[] = { "mcu", "systime", "sig", "as5311", "dro" };
const char *field_names[][MAX_VALUES] = {
	{ "dir", "data", },
	{ "type", },
	{ "data", "valid", "cnt", },
	{ "channel", "type", "pos", "status", },
	{ "channel", "data" },
};

#define MAX_MODELS	100
int nmodels = 0;
void (*models[MAX_MODELS])(void *ctx, buffer_elem_t *be);
void *mctx[MAX_MODELS];
int verbose = 0;
static int dont_sort = 0;
static int re_init_allowed = 0;

int do_output = 1;

static void
output(parser_t *p, uint64_t systime, int type, int n, value_t *v);

void __attribute__ ((format (printf, 1, 2)))
report(char *fmt, ...)
{
	va_list ap;

	fflush(stdout);
	va_start(ap, fmt);
#if 0
	vfprintf(stderr, fmt, ap);
#else
	vfprintf(stdout, fmt, ap);
#endif
	va_end(ap);
	fflush(stdout);

	abort();
}

static void
init_signal(signal_t *s)
{
	memset(s, 0, sizeof(*s));
	s->state = SS_IDLE;
	s->first_packet = 1;
}

static void
sig_output(parser_t *p, pipeline_t sample, int n)
{
	signal_t *s = &p->signal;
	value_t v[3];

	v[0].type = VT_HEX;
	v[0].ui = sample.data;
	v[1].type = VT_UINT;
	v[1].ui = sample.valid;
	v[2].type = VT_UINT;
	v[2].ui = n;

	output(p, s->systime, MT_SIG, 3, v);

	s->systime += n;
}

static int
sig_get_bits(signal_t *s, int n, uint32_t *out)
{
	if (s->buflen < n)
		return 0;

	*out = (s->buf >> (s->buflen - n)) & ((1 << n) - 1);
	lD("get bits: buf %016lx len %d n %d out %x\n", s->buf, s->buflen,
		n, *out);
	s->buflen -= n;

	return 1;
}

static void
sig_feed(parser_t *p, uint32_t d, int len)
{
	signal_t *s = &p->signal;
	int i;
	uint32_t bits;
	pipeline_t sample;

	lD("sig_feed: in %08x\n", d);
	s->buf <<= len;
	s->buf |= d;
	s->buflen += len;

	while (1) {
		if (s->state == SS_IDLE) {
			if (!sig_get_bits(s, 3, &bits))
				break;
			s->slot = bits;

			if (bits == 0)
				s->state = SS_SAMPLE;
			else
				s->state = SS_CNT_PRE;
		} else if (s->state == SS_SAMPLE) {
			if (!sig_get_bits(s, s->width, &bits))
				break;
			memmove(s->pipeline + 1, s->pipeline + 0,
				sizeof(*s->pipeline) * 6);
			s->pipeline[0].data = bits;
			s->pipeline[0].valid = 1;
			sig_output(p, s->pipeline[0], 1);
			s->state = SS_IDLE;
		} else if (s->state == SS_CNT_PRE) {
			if (!sig_get_bits(s, 5, &bits))
				break;
			if ((bits & 0x10) == 0) {
				s->cnt = bits & 0x0f;
				s->state = SS_CNT_DONE;
			} else if ((bits & 0x18) == 0x10) {
				s->cnt = bits & 0x07;
				s->state = SS_CNT_8;
			} else if ((bits & 0x1c) == 0x18) {
				s->cnt = bits & 0x03;
				s->state = SS_CNT_RLE;
			} else {
				report("invalid cnt encoding in sig stream\n");
			}
		} else if (s->state == SS_CNT_8) {
			if (!sig_get_bits(s, 5, &bits))
				break;
			s->cnt = (s->cnt << 5) | bits;
			s->state = SS_CNT_DONE;
		} else if (s->state == SS_CNT_RLE) {
			if (!sig_get_bits(s, s->rle - 2, &bits))
				break;
			s->cnt = (s->cnt << (s->rle - 2)) | bits;
			s->state = SS_CNT_DONE;
		} else if (s->state == SS_CNT_DONE) {
			if (s->slot != 1) {
				for (i = 0; i < s->cnt; ++i) {
					sample = s->pipeline[s->slot - 1];
					memmove(s->pipeline + 1,
						s->pipeline + 0,
						sizeof(*s->pipeline) *
							(s->slot - 1));
					s->pipeline[0] = sample;
					sig_output(p, sample, 1);
				}
			} else {
				/*
				 * this is just a performance
				 * optimization
				 */
				sample = s->pipeline[s->slot - 1];
				sig_output(p, sample, s->cnt);
			}
			s->state = SS_IDLE;
		} else {
			report("inval sig state %d\n", s->state);
		}
	}
}

static void
init_parser(parser_t *p)
{
	int i;

	memset(p, 0, sizeof(*p));
	p->seq = -1;
	p->first_frame = 1;
	init_signal(&p->signal);

	for (i = 0; i < nmodels; ++i) {
		/* XXX well ... */
		if (models[i] == model1)
			mctx[i] = init_model1(p, verbose);
		if (models[i] == model2)
			mctx[i] = init_model2(p, verbose);
	}
}

uint64_t
relate_systime(parser_t *p, uint64_t systime, int bits)
{
	int s1;
	int s2;
	int diff;
	uint64_t rel;

	/*
	 * systime should lie within +/- 1/4 of the range given by bits
	 * of the current base. otherwise we refuse to relate it.
	 * As the base is updated in steps of 2^28, this is the minimum
	 * time we accept to relate
	 */
	if (bits < 28)
		report("can't relate systimes with less than 28 bits\n");

	if (!p->base_set)
		return systime;

	s1 = (systime >> (bits - 2)) & 0x03;
	s2 = (p->systime_base >> (bits - 2)) & 0x03;
	diff = (s2 - s1) & 0x03;

	if (diff == 2 || diff == 3)
		report("systime %ld out of range of %ld\n", systime,
			p->systime_base);

	rel = (p->systime_base & ~((1ull << bits) - 1)) | systime;
	if (s2 == 0 && s1 == 3)
		rel -= (1ull << bits);
#if 0
printf("relate: s1 %d s2 %d systime %lx base %lx rel %lx %ld\n", s1, s2, systime, p->systime_base, rel, rel);
#endif

	return rel;
}

static int
recv_mcu(parser_t *p, int type, uint32_t *b, int len)
{
	uint64_t systime;
	uint8_t data;
	value_t v[2];

	systime = b[0] & 0xffff;
	data = (b[0] >> 16) & 0xff;

	if (type == DAQT_MCU_RX_LONG || type == DAQT_MCU_TX_LONG) {
		systime |= (uint64_t)b[1] << 16;
		p->mcu_systime = systime & ~0xffff;
		p->mcu_systime_set = 1;
	} else {
		if (!p->mcu_systime_set)
			systime = 0;
		else
			systime |= p->mcu_systime;
	}

	if (systime != 0)
		systime = relate_systime(p, systime, 48);


	v[0].type = VT_STRING;
	if (type == DAQT_MCU_RX_LONG || type == DAQT_MCU_RX) {
		v[0].s = "rx";
	} else {
		v[0].s = "tx";
	}
	v[1].type = VT_HEX;
	v[1].ui = data;
	output(p, systime, MT_MCU, 2, v);

	return (type == DAQT_MCU_RX_LONG || type == DAQT_MCU_TX_LONG) ? 2 : 1;
}

static int
recv_as5311(parser_t *p, int type, uint32_t *b, int len)
{
	uint32_t data;
	uint64_t systime;
	int channel;
	int pos;
	char status[100] = ".";
	int par = 0;
	int i;
	value_t v[4];

	channel = (b[0] >> 18) & 0x3f;
	data = b[0] & 0x3ffff;
	systime = b[1];
	systime = relate_systime(p, systime, 32);
	pos = data >> 6;

	if (data & 0x20)
		strcat(status, "OCF.");
	if (data & 0x10)
		strcat(status, "COF.");
	if (data & 0x8)
		strcat(status, "LIN.");
	if (data & 0x4)
		strcat(status, "INC.");
	if (data & 0x2)
		strcat(status, "DEC.");
	for (i = 0; i < 18; ++i)
		if (data & (1 << i))
			par = !par;
	if (par)
		strcat(status, "PAR.");

	v[0].type = VT_UINT;
	v[0].ui = channel;
	v[1].type = VT_STRING;
	v[1].s = type == DAQT_AS5311_DAT ? "dat" : "mag";
	v[2].type = VT_UINT;
	v[2].ui = pos;
	v[3].type = VT_STRING;
	v[3].s = status;
	output(p, systime, MT_AS5311, 4, v);

	return 2;
}

static int
recv_systime(parser_t *p, int type, uint32_t *b, int len)
{
	uint64_t systime;
	value_t v;

	systime = ((uint64_t)(b[0] & 0xffffff) << 32) + b[1];

	v.type = VT_STRING;
	v.s = type == DAQT_SYSTIME_SET ? "set" : "rollover";
	output(p, systime, MT_SYSTIME, 1, &v);

	/*
	 * sanity check if we lost a packet. we could as well cope with the
	 * occasional loss
	 */
	if (p->base_set && systime - p->systime_base > (1ull << 28))
		report("unexpected jump in systime\n");

	p->systime_base = systime;
	p->base_set = 1;

	return 2;
}

static int
recv_dro_data(parser_t *p, uint32_t *b, int len)
{
	uint8_t channel = (b[0] >> 16) & 0xff;
	uint8_t bits = (b[0] >> 8) & 0xff;
	uint32_t d = b[1];
	uint64_t systime = b[2];
	uint32_t data = 0;
	int i;
	int sign;
	float val;
	value_t v[2];

	systime = relate_systime(p, systime, 32);

	if (bits != 24)
		report("dro: bad bit length\n");

	for (i = 0; i < bits; ++i) {
		data <<= 1;
		data |= d & 1;
		d >>= 1;
	}
	sign = data & (1 << 20);
	val = (data & ((1 << 20) - 1)) / 1000.;
	if (sign)
		val = -val;

	v[0].type = VT_UINT;
	v[0].ui = channel;
	v[1].type = VT_FLOAT;
	v[1].f = val;
	output(p, systime, MT_DRO, 2, v);

	return 3;
}

static int
recv_signal_data(parser_t *p, uint32_t *b, int len)
{
	signal_t *s = &p->signal;
	uint8_t off = (b[0] >> 18) & 0x1f;
	uint8_t rle = (b[0] >> 13) & 0x1f;
	uint8_t width = (b[0] >> 8) & 0x1f;
	uint8_t stlen = b[0] & 0xff;
	uint64_t systime = b[1];
	int i = 0;

	if (s->first_packet)
		s->systime = systime;

	if (!s->systime_related && p->base_set) {
		s->systime = relate_systime(p, s->systime, 32);
		s->systime_related = 1;
	}

	lD("%ld signal width %d rle %d off %d len %d\n",
		relate_systime(p, systime, 32),
		width, rle, off, stlen);

	if (s->width != 0 && s->width != width)
		report("signal: width changed mid-stream\n");
	s->width = width;

	if (s->rle != 0 && s->rle != rle)
		report("signal: rle changed mid-stream\n");
	s->rle = rle;

	if (s->first_packet && off) {
		sig_feed(p, b[2] & ((1 << (32 - off)) - 1), 32 - off);
		i = 1;
	}
	s->first_packet = 0;

	for (; i < stlen; ++i)
		sig_feed(p, b[2 + i], 32);

	return stlen + 2;
}

static void
parse_frame(parser_t *p, uint8_t *pbuf, int len)
{
	uint32_t *b = (uint32_t *)(pbuf + 2);
	uint16_t seq;
	int l;
	int i;

	if ((len % 4) != 2)
		report("frame with unaligned len %d\n", len);
	if (len < 6)
		report("short frame with len %d\n", len);

	seq = pbuf[0] * 256 + pbuf[1];
	lD("len %d seq %d\n", len, seq);
	if (p->first_frame) {
		p->seq = seq - 1;
		p->first_frame = 0;
	}
	++p->seq;
	if (seq != p->seq) {
		if (!re_init_allowed || seq != 0)
			report("bad seq in frame, expected %d got %d\n",
				p->seq + 1, seq);
		/*
		 * currently not reached. we could re-init the parser and
		 * start over
		 */
		printf("re-initializing parser\n");
		printf(">>>>>> CUT HERE <<<<<<<\n");
		fprintf(stderr, "re-initializing parser\n");
		init_parser(p);
		parse_frame(p, pbuf, len);
		return;
	}
	len = (len - 2) / 4;
	/* bring into host byte order */
	for (i = 0; i < len; ++i)
		b[i] = ntohl(b[i]);

	while (len > 0) {
		uint8_t type = b[0] >> 24;
		lD("data %08x %08x %08x\n", b[0], b[1], b[2]);
		switch (type) {
		case DAQT_MCU_RX:
		case DAQT_MCU_RX_LONG:
		case DAQT_MCU_TX:
		case DAQT_MCU_TX_LONG:
			l = recv_mcu(p, type, b, len);
			break;
		case DAQT_AS5311_DAT:
		case DAQT_AS5311_MAG:
			l = recv_as5311(p, type, b, len);
			break;
		case DAQT_SYSTIME_SET:
		case DAQT_SYSTIME_ROLLOVER:
			l = recv_systime(p, type, b, len);
			break;
		case DAQT_DRO_DATA:
			l = recv_dro_data(p, b, len);
			break;
		case DAQT_SIGNAL_DATA:
			l = recv_signal_data(p, b, len);
			break;
		case 0xff:
			l = 1;
			break;
		default:
			report("unknown packet type %d\n", type);
			break;
		}
		lD("type %d len %d l %d\n", type, len, l);
		b += l;
		len -= l;
	}
}

int becmp(buffer_elem_t *, buffer_elem_t *);
RB_HEAD(betree, _buffer_elem) behead = RB_INITIALIZER(&behead);
RB_PROTOTYPE(betree, _buffer_elem, rbnode, becmp)
RB_GENERATE(betree, _buffer_elem, rbnode, becmp)

int becmp(buffer_elem_t *e1, buffer_elem_t *e2)
{
	if (e1->systime < e2->systime)
		return -1;
	if (e1->systime > e2->systime)
		return 1;
	return 0;
}

static void
output_be(parser_t *p, buffer_elem_t *be)
{
	int i;

	/*
	 * feed to models
	 */
	for (i = 0; i < nmodels; ++i)
		models[i](mctx[i], be);

	if (!do_output)
		goto free;

	if (be->systime == 0)
		printf("-------------- ");
	else
		printf("% 14ld ", be->systime);

	printf("%s", type_names[be->mtype]);

	for (i = 0; i < be->n; ++i) {
		if (be->values[i].type == VT_UINT) {
			printf(" %s=%ld", field_names[be->mtype][i],
				be->values[i].ui);
		} else if (be->values[i].type == VT_HEX) {
			printf(" %s=%lx", field_names[be->mtype][i],
				be->values[i].ui);
		} else if (be->values[i].type == VT_FLOAT) {
			printf(" %s=%lf", field_names[be->mtype][i],
				be->values[i].f);
		} else if (be->values[i].type == VT_STRING) {
			printf(" %s=%s", field_names[be->mtype][i],
				be->values[i].s);
		}
	}
	printf("\n");

free:
	for (i = 0; i < be->n; ++i)
		if (be->values[i].type == VT_STRING)
			free(be->values[i].s);
	free(be->values);
	free(be);
}

static void
output(parser_t *p, uint64_t systime, int mtype, int n, value_t *values_in)
{
	buffer_elem_t *be;
	int i;
	value_t *values;

	if (n > MAX_VALUES)
		report("too many values, increase MAX_VALUES\n");

	/*
	 * copy values
	 */
	values = malloc(sizeof(*values) * n);
	memcpy(values, values_in, sizeof(*values) * n);
	for (i = 0; i < n; ++i) {
		if (values_in[i].type == VT_STRING)
			values[i].s = strdup(values_in[i].s);
	}

	if (!p->base_set)
		systime = 0;

	/*
	 * data might arrive a bit out of order. delay everything to sort
	 * it. Max delay is around 200ms, 100ms timeout for sig plus 100ms
	 * timeout for ethernet.
	 */
	be = malloc(sizeof(*be));
	assert(be);
	be->systime = systime;
	be->mtype = mtype;
	be->n = n;
	be->values = values;

	if (systime == 0 || dont_sort) {
		/* no systime, output directly */
		output_be(p, be);
		return;
	}

	RB_INSERT(betree, &behead, be);

	if (systime > p->newest_systime) {
		if (p->newest_systime != 0 &&
		    systime - p->newest_systime > RETENTION_CYCLES) {
			report("%ld big jump in systime by %ld\n", systime,
				systime - p->newest_systime);
		}
		p->newest_systime = systime;
	}

	if (systime < p->newest_delivered)
		report("%ld output arrived already after delay phase by %ld\n",
			systime, p->newest_delivered - systime);

	while (1) {
		be = RB_MIN(betree, &behead);

		if (be->systime + RETENTION_CYCLES > p->newest_systime)
			break;

		RB_REMOVE(betree, &behead, be);

		p->newest_delivered = be->systime;

		output_be(p, be);
	}

	return;
}

void
usage(void)
{
	printf("usage: conan_cap [options]\n");
	printf("       -r <file>   : read input from file\n");
	printf("       -w <file>   : write capture to file\n");
	printf("       -v          : verbose mode\n");
	printf("       -q          : quiet, no direct output\n");
	printf("       -1          : feed to model 1\n");
	printf("       -u          : unsorted, don't sort by timestamp\n");
	printf("       -i          : re-init state on a frame-seq reset\n");
	exit(1);
}

int
main(int argc, char **argv)
{
	int pfd = -1;
	int len;
	uint8_t buf[1520];
	int  c;
	char *wrname = NULL;
	char *rdname = NULL;
	int outfd = -1;
	int infd = -1;
	int ret;
	parser_t parser;

	while ((c = getopt(argc, argv, "w:r:vuq12ih?")) != EOF) {
		switch(c) {
		case 'w':
			wrname = optarg;
			break;
		case 'r':
			rdname = optarg;
			break;
		case 'u':
			dont_sort = 1;
			break;
		case 'v':
			verbose = 1;
			break;
		case 'q':
			do_output = 0;
			break;
		case 'i':
			re_init_allowed = 1;
			break;
		case '1':
			mctx[nmodels] = init_model1(&parser, verbose);
			models[nmodels++] = model1;
			break;
		case '2':
			mctx[nmodels] = init_model2(&parser, verbose);
			models[nmodels++] = model2;
			break;
		case 'h':
		case '?':
		default:
			usage();
		}
	}

	if (!wrname)
		init_parser(&parser);

	if (rdname != NULL && wrname != NULL) {
		printf("-r and -w can't be used simultaneously\n");
		exit(1);
	}
	if (wrname) {
		outfd = open(wrname, O_WRONLY | O_CREAT | O_TRUNC, 0666);
		if (outfd == -1) {
			perror("open output");
			exit(1);
		}
		ret = write(outfd, "conancap", 8);
		if (ret == -1) {
			perror("write");
			exit(1);
		}
		if (ret != 8) {
			printf("short write\n");
			exit(1);
		}
	}
	if (rdname) {
		char buf[8];
		infd = open(rdname, O_RDONLY);
		if (infd == -1) {
			perror("open input");
			exit(1);
		}
		ret = read(infd, buf, 8);
		if (ret == -1) {
			perror("read");
			exit(1);
		}
		if (ret != 8) {
			printf("short read\n");
			exit(1);
		}

		if (strncmp(buf, "conancap", 8) != 0) {
			printf("invalid signature in capture file\n");
			exit(1);
		}
	} else {
		pfd = socket(AF_PACKET, SOCK_DGRAM, htons(0x5139));
		if (pfd == -1) {
			perror("socket");
			exit(1);
		}
	}

	while (1) {
		if (rdname) {
			uint32_t l;

			ret = read(infd, &l, 4);
			if (ret == 0)
				exit(0);
			if (ret == 4) {
				len = ntohl(l);
				if (len > 1520) {
					printf("oversized pkt in capture\n");
					exit(1);
				}
				ret = read(infd, buf, len);
			}
			if (ret == -1) {
				perror("read");
				exit(1);
			}
			if (ret != len) {
				printf("short read\n");
				exit(1);
			}
		} else {
			len = recv(pfd, buf, sizeof(buf), 0);
			if (len == -1) {
				perror("recv");
				exit(1);
			}
		}

		if (wrname) {
			uint32_t l = htonl(len);
			ret = write(outfd, &l, 4);
			if (ret == 4)
				ret = write(outfd, buf, len);
			if (ret == -1) {
				perror("write");
				exit(1);
			}
			if (ret != len) {
				printf("short write\n");
				exit(1);
			}
			printf("%d\n", len);
		} else {
			parse_frame(&parser, buf, len);
		}
	}
	close(pfd);

	exit(0);
}
