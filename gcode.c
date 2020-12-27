#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef unsigned long __uintptr_t;
#include "tree.h"
#include "models.h"

static FILE *outfp = NULL;

void enable_gcode(char *filename)
{
	outfp = fopen(filename, "w+");
	if (outfp == NULL) {
		perror("opening gcode output");
		exit(1);
	}
}

void
gcode(buffer_elem_t *be, double time,
	double as_x1, double as_x2, double as_y,
	double x, double y, double z1, double z2, double z3,
	double e, double dr0)
{
	if (outfp == NULL)
		return;

	printf("gcode out\n");
}
