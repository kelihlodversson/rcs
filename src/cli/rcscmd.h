/* RCSCMD.H	the commands of rcs-cli (rcscmd.c, docs/rcs-cli.md) */
#ifndef RCSCMD_H
#define RCSCMD_H

#include <stddef.h>
#include "rscmodel.h"
#include "rscnames.h"
#include "rcsnode.h"

/* The most second files (FILE.rsc:/PATH) one run opens. */
#define RCS_OTHER_MAX	8

/* A resource file with its names and a view of the two: the main file, or
 * a second file the copy commands read from (never written). */
struct rcs_src
{
	struct rsc		*r;
	struct rsc_names	*n;
	struct rcs_view		*v;
	char			*file;		/* a second file's name (malloc'd); NULL for the main file */
	int			is_main;
};

/* What the commands work on: the resource as read (its cell size says
 * whether coordinates are shown in pixels), its names (never NULL) and a
 * view of the two. */
struct rcs_ctx
{
	struct rsc		*r;
	struct rsc_names	*n;
	struct rcs_view		*v;
	const char		*file;		/* the name of the main file (NULL: unknown) */
	int			edited;		/* an edit command ran and succeeded */
	int			unchanged;	/* set by a command that succeeded but changed nothing (rcs_run clears it) */
	int			no_guess;	/* --no-guess: new trees and alert strings stay unnamed */
	int			selfcheck;	/* --selfcheck: a failed edit command must not change the model */
	/* Opens a second file with the options of the run: fills *out (r, n, v)
	 * and returns 0, or returns -1 after an error line. */
	int			(*load)(struct rcs_ctx *c, const char *file, struct rcs_src *out);
	struct rcs_src		other[RCS_OTHER_MAX];
	int			nother;
};

/* Runs one command (argv[0] is its name) and returns 0, or 1 after an
 * error line on stderr. */
int rcs_run(struct rcs_ctx *c, int argc, char **argv);

/* Frees the second files of c (not the main file). */
void rcs_ctx_free_others(struct rcs_ctx *c);

/* Whether command cmd changes the resource (cp mv rm mkdir touch chmod set). */
int rcs_is_edit(const char *cmd);

/* Writes len bytes to a temporary file next to path (path.XXXXXX) and
 * renames it over path once it is written and closed; the mode is that of
 * an existing regular file path when keep_mode, else the one fopen would
 * give for the umask.  Returns 0, or -1 with the reason (strerror text) in
 * why (size bytes); then no temporary file is left and path is as it was. */
int rcs_write_file(const char *path, const void *buf, size_t len, int keep_mode, char *why, size_t size);

/* Prints an error line "rcs-cli: " fmt to stderr. */
void rcs_error(const char *fmt, ...)
#if defined(__GNUC__)
	__attribute__((format(printf, 1, 2)))
#endif
	;

/* Prints a note line "rcs-cli: note: " fmt to stderr. */
void rcs_note(const char *fmt, ...)
#if defined(__GNUC__)
	__attribute__((format(printf, 1, 2)))
#endif
	;

#endif
