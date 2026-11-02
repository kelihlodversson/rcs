/* RCSCMD.H	the commands of rcs-cli (rcscmd.c, docs/rcs-cli.md) */
#ifndef RCSCMD_H
#define RCSCMD_H

#include "rscmodel.h"
#include "rscnames.h"
#include "rcsnode.h"

/* What the commands work on: the resource as read (its cell size says
 * whether coordinates are shown in pixels), its names (may be NULL) and a
 * view of the two. */
struct rcs_ctx
{
	struct rsc		*r;
	struct rsc_names	*n;
	struct rcs_view		*v;
};

/* Runs one command (argv[0] is its name) and returns 0, or 1 after an
 * error line on stderr. */
int rcs_run(struct rcs_ctx *c, int argc, char **argv);

/* Prints an error line "rcs-cli: " fmt to stderr. */
void rcs_error(const char *fmt, ...)
#if defined(__GNUC__)
	__attribute__((format(printf, 1, 2)))
#endif
	;

#endif
