/* RCS-CLI.C	rcs-cli: options, loading the resource and its names, -c
 *		handling, exit status (docs/rcs-cli.md)
 *
 *	rcs-cli [OPTIONS] FILE.rsc COMMAND [ARGS...]
 *	rcs-cli [OPTIONS] FILE.rsc -c 'COMMAND ARGS' -c 'COMMAND ARGS' ...
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "rscfile.h"
#include "rscnames.h"
#include "rcscmd.h"

/* The largest resource file and name file read. */
#define RSC_FILE_MAX	(64L * 1024 * 1024)
#define NAMES_FILE_MAX	(16L * 1024 * 1024)
/* The most words of one -c command. */
#define WORDS_MAX	64

static const char usage_text[] =
	"usage: rcs-cli [OPTIONS] FILE.rsc COMMAND [ARGS...]\n"
	"       rcs-cli [OPTIONS] FILE.rsc -c 'COMMAND ARGS' [-c 'COMMAND ARGS'...]\n"
	"\n"
	"options (before FILE.rsc):\n"
	"  --names FILE     read the names from FILE (.dfn, .def, .rsd, .hrd)\n"
	"  --no-names       read no name file\n"
	"  --no-guess       do not guess names and tree kinds\n"
	"  --pixels[=WxH]   coordinates in pixels, cell WxH (default 8x16)\n"
	"  -o OUT.rsc       write the result of the edit commands to OUT.rsc\n"
	"  -n               dry run: run the commands, write nothing\n"
	"  --names-out FILE write the names to FILE (.dfn)\n"
	"  --help           this text\n"
	"\n"
	"commands:\n"
	"  ls [-l] [-R] [PATH]   list the children of PATH (default /)\n"
	"  tree [PATH]           draw the subtree of PATH\n"
	"  stat PATH             every property of a node\n"
	"  cat [-v] PATH         the text of a node\n"
	"  convert -o OUT.rsc    write the resource with the library's writer\n"
	"  cp [-r] SRC DST       copy (SRC may be FILE.rsc:/PATH)\n"
	"  mv SRC DST            move or rename\n"
	"  rm [-r] PATH          remove\n"
	"  mkdir NAME [--type dialog|menu|panel|alert]   new tree\n"
	"  touch PATH [--type T] new object, string or image\n"
	"  chmod [--state] [+-=]NAME[,NAME...]... PATH   flags and states\n"
	"  set PATH key=value... change an object\n";

/* A name for messages: at most 200 bytes of s, escaped. */
static const char *esc(char *buf, size_t size, const char *s)
{
	rcs_escape(buf, size, s, 200, 1);
	return buf;
}

/* Reads the regular file name, at most max bytes, into *buf (malloc'd) and
 * *len; 0, or -1 after an error line. */
static int load(const char *name, long max, uint8_t **buf, size_t *len)
{
	struct stat	st;
	FILE		*f;
	uint8_t		*b;
	size_t		n = 0, got;
	char		eb[1024];

	*buf = NULL;
	*len = 0;
	if (stat(name, &st) != 0)
	{
		rcs_error("%s: cannot open: %s", esc(eb, sizeof eb, name), strerror(errno));
		return -1;
	}
	if (!S_ISREG(st.st_mode))
	{
		rcs_error("%s: not a regular file", esc(eb, sizeof eb, name));
		return -1;
	}
	if (st.st_size > max)
	{
		rcs_error("%s: file too large", esc(eb, sizeof eb, name));
		return -1;
	}
	f = fopen(name, "rb");
	if (!f)
	{
		rcs_error("%s: cannot open: %s", esc(eb, sizeof eb, name), strerror(errno));
		return -1;
	}
	/* the size may change under us: read at most max + 1 bytes */
	b = malloc((size_t)st.st_size + 1);
	if (!b)
	{
		fclose(f);
		rcs_error("%s: out of memory", esc(eb, sizeof eb, name));
		return -1;
	}
	while (n < (size_t)st.st_size + 1)
	{
		got = fread(b + n, 1, (size_t)st.st_size + 1 - n, f);
		if (got == 0)
			break;
		n += got;
	}
	if (ferror(f) || n > (size_t)st.st_size)
	{
		int	e = errno;

		fclose(f);
		free(b);
		if (n > (size_t)st.st_size)
			rcs_error("%s: the file changed while it was read", esc(eb, sizeof eb, name));
		else
			rcs_error("%s: cannot read: %s", esc(eb, sizeof eb, name), strerror(e));
		return -1;
	}
	fclose(f);
	*buf = b;
	*len = n;
	return 0;
}

static int same_path(const char *a, const char *b)
{
	struct stat	sa, sb;

	return stat(a, &sa) == 0 && stat(b, &sb) == 0 && sa.st_dev == sb.st_dev && sa.st_ino == sb.st_ino;
}

static int is_file(const char *name)
{
	struct stat	st;

	return stat(name, &st) == 0 && S_ISREG(st.st_mode);
}

/* The name file next to file: the same base name with .dfn .def .rsd .hrd,
 * each in lower then upper case; the first that exists, malloc'd, or
 * NULL. */
static char *find_names(const char *file)
{
	static const char *const ext[] = { ".dfn", ".DFN", ".def", ".DEF", ".rsd", ".RSD", ".hrd", ".HRD" };
	const char	*slash = strrchr(file, '/'), *dot = strrchr(file, '.');
	size_t		base, i;
	char		*cand;

	if (dot && (!slash || dot > slash))
		base = (size_t)(dot - file);
	else
		base = strlen(file);
	cand = malloc(base + 5);
	if (!cand)
		return NULL;
	memcpy(cand, file, base);
	for (i = 0; i < sizeof ext / sizeof ext[0]; i++)
	{
		memcpy(cand + base, ext[i], 5);
		if (is_file(cand))
			return cand;
	}
	free(cand);
	return NULL;
}

/* Splits a -c string into words: blanks (space, tab) separate; single
 * quotes group literally; double quotes group, and a backslash in them
 * takes the next character literally; outside quotes a backslash takes the
 * next character literally too.  The words are written into out (as long
 * as s plus one NUL per word) and listed in words[]; returns their number,
 * or -1 for an unterminated quote, a trailing backslash or too many
 * words. */
static int split(const char *s, char *out, char **words, int max, const char **why)
{
	int	n = 0, in = 0;
	char	q = 0;

	for (;;)
	{
		char	ch = *s;

		if (ch == 0)
			break;
		if (!q && (ch == ' ' || ch == '\t'))
		{
			if (in)
			{
				*out++ = 0;
				in = 0;
			}
			s++;
			continue;
		}
		if (!in)
		{
			if (n == max)
			{
				*why = "too many words";
				return -1;
			}
			words[n++] = out;
			in = 1;
		}
		s++;
		if (q == '\'')
		{
			if (ch == '\'')
				q = 0;
			else
				*out++ = ch;
		}
		else if (ch == '\\' && q != '\'')
		{
			if (*s == 0)
			{
				*why = "trailing backslash";
				return -1;
			}
			*out++ = *s++;
		}
		else if (q == '"')
		{
			if (ch == '"')
				q = 0;
			else
				*out++ = ch;
		}
		else if (ch == '\'' || ch == '"')
			q = ch;
		else
			*out++ = ch;
	}
	if (q)
	{
		*why = "unterminated quote";
		return -1;
	}
	if (in)
		*out = 0;
	return n;
}

/* Parses W x H of --pixels=WxH, each 1..RSC_MAX_CELL. */
static int parse_cell(const char *s, int *w, int *h)
{
	long	a = 0, b = 0;
	int	da = 0, db = 0;

	for (; *s >= '0' && *s <= '9' && da < 4; s++, da++)
		a = a * 10 + (*s - '0');
	if (*s != 'x' && *s != 'X')
		return -1;
	for (s++; *s >= '0' && *s <= '9' && db < 4; s++, db++)
		b = b * 10 + (*s - '0');
	if (*s || !da || !db || a < 1 || b < 1 || a > RSC_MAX_CELL || b > RSC_MAX_CELL)
		return -1;
	*w = (int)a;
	*h = (int)b;
	return 0;
}

/* What the loading of a resource and its names follows (the options). */
struct opts
{
	int	cw, ch;
	int	no_names, no_guess;
};

static struct opts	g_opts;

static int has_ext(const char *name, const char *ext)
{
	size_t	n = strlen(name), m = strlen(ext), i;

	if (n < m)
		return 0;
	for (i = 0; i < m; i++)
	{
		int	a = (unsigned char)name[n - m + i];

		if (a >= 'A' && a <= 'Z')
			a += 'a' - 'A';
		if (a != ext[i])
			return 0;
	}
	return 1;
}

/* Reads a resource file and its names (names_file, or the one found next to
 * it unless the options say none) and guesses the names.  *names is never
 * NULL on success; *used is the name file read (malloc'd) or NULL.  0, or -1
 * after an error line. */
static int load_pair(const char *file, const char *names_file, struct rsc **rp, struct rsc_names **np,
		     char **used, int *dropped_out)
{
	struct rsc_error	err;
	struct rsc		*r;
	struct rsc_names	*n = NULL;
	uint8_t			*buf = NULL;
	size_t			len = 0;
	char			*found = NULL, eb[1024];
	int			dropped = 0;

	*rp = NULL;
	*np = NULL;
	*used = NULL;
	if (dropped_out)
		*dropped_out = 0;
	if (load(file, RSC_FILE_MAX, &buf, &len) != 0)
		return -1;
	r = rsc_atari_read(buf, len, g_opts.cw, g_opts.ch, &err);
	free(buf);
	buf = NULL;
	if (!r)
	{
		rcs_error("%s: %s", esc(eb, sizeof eb, file), err.msg);
		return -1;
	}
	if (!names_file && !g_opts.no_names)
		names_file = found = find_names(file);
	if (names_file)
	{
		if (load(names_file, NAMES_FILE_MAX, &buf, &len) != 0)
			goto fail;
		n = rsc_names_read_by_ext(names_file, buf, len, &err);
		free(buf);
		if (!n)
		{
			rcs_error("%s: %s", esc(eb, sizeof eb, names_file), err.msg);
			goto fail;
		}
		if (rsc_names_check(n, r, &dropped) != 0)
		{
			rcs_error("%s: cannot check the names", esc(eb, sizeof eb, names_file));
			goto fail;
		}
		if (dropped)
			rcs_error("warning: %d name entries do not match the resource and were ignored", dropped);
		if (dropped_out)
			*dropped_out = dropped;
	}
	if (!n)
		n = rsc_names_new();
	if (!n || (!g_opts.no_guess && rsc_names_guess(n, r) < 0))
	{
		rcs_error("%s: cannot guess the names: the name list has no room for them "
			  "(at most 65535 entries) or memory ran out; use --no-guess",
			  esc(eb, sizeof eb, file));
		goto fail;
	}
	if (names_file && !found)
		found = strdup(names_file);
	*rp = r;
	*np = n;
	*used = found;
	return 0;
fail:
	rsc_names_free(n);
	rsc_free(r);
	free(found);
	return -1;
}

static int load_other(struct rcs_ctx *c, const char *file, struct rcs_src *out)
{
	char	*used;

	(void)c;
	if (load_pair(file, NULL, &out->r, &out->n, &used, NULL) != 0)
		return -1;
	free(used);
	out->v = rcs_view_new(out->r, out->n);
	if (!out->v)
	{
		rsc_names_free(out->n);
		rsc_free(out->r);
		rcs_error("out of memory");
		return -1;
	}
	return 0;
}

/* The base name of file (without its last extension) plus ext. */
static char *with_ext(const char *file, const char *ext)
{
	const char	*slash = strrchr(file, '/'), *dot = strrchr(file, '.');
	size_t		base = dot && (!slash || dot > slash) ? (size_t)(dot - file) : strlen(file);
	char		*r = malloc(base + strlen(ext) + 1);

	if (!r)
		return NULL;
	memcpy(r, file, base);
	strcpy(r + base, ext);
	return r;
}

/* Entries of the names that a .dfn file cannot hold, among those it would
 * write: the number, and the first as a message in *first. */
static int dfn_misfits(const struct rsc_names *n, const struct rsc *r, char *first, size_t size)
{
	struct rsc_name	e;
	int		i, cnt = rsc_names_count(n), bad = 0, nobj, ok;
	char		eb[100];

	for (i = 0; i < cnt; i++)
	{
		if (rsc_names_get(n, i, &e) != 0 || e.guessed || e.kind == RSC_NAME_UNKNOWN)
			continue;
		switch (e.kind)
		{
		case RSC_NAME_TREE:
			ok = e.tree < rsc_ntrees(r);
			break;
		case RSC_NAME_OBJ:
			ok = e.tree < rsc_ntrees(r) && rsc_tree_objects(r, e.tree, &nobj) && e.obj < nobj;
			break;
		case RSC_NAME_FRSTR:
			ok = e.obj < rsc_nfrstr(r);
			break;
		default:
			ok = e.obj < rsc_nfrimg(r);
			break;
		}
		if (!ok || (strlen(e.name) <= 8 && !(e.kind == RSC_NAME_OBJ && (e.tree > 255 || e.obj > 255))))
			continue;
		if (!bad)
		{
			if (e.kind == RSC_NAME_OBJ)
				snprintf(first, size, "'%s' (tree %d, object %d)", esc(eb, sizeof eb, e.name), e.tree, e.obj);
			else
				snprintf(first, size, "'%s' (%s %d)", esc(eb, sizeof eb, e.name),
					 e.kind == RSC_NAME_TREE ? "tree" : e.kind == RSC_NAME_FRSTR ? "free string"
					 : "free image", e.kind == RSC_NAME_TREE ? e.tree : e.obj);
		}
		bad++;
	}
	return bad;
}

/* Renumbers the names of every tree as the writer numbers the objects (in
 * preorder; the objects no root reaches are not written, their names go).
 * 0, or -1 (no memory). */
static int preorder_names(struct rcs_ctx *c)
{
	int		t, n, k, i, *order, *map, nt = rsc_ntrees(c->r), rc = 0;
	const OBJECT	*o;

	for (t = 0; t < nt && rc == 0; t++)
	{
		o = rsc_tree_objects(c->r, t, &n);
		order = malloc((size_t)n * sizeof *order);
		map = malloc((size_t)n * sizeof *map);
		if (!o || !order || !map)
			rc = -1;
		else
		{
			k = rsc_tree_preorder(o, n, order);
			if (k < 0)
				rc = -1;
			else
			{
				for (i = 0; i < n; i++)
					map[i] = -1;
				for (i = 0; i < k; i++)
					map[order[i]] = i;
				if (rsc_names_renumber(c->n, t, map, n) != 0)
					rc = -1;
			}
		}
		free(order);
		free(map);
	}
	return rc;
}

struct plan
{
	const char	*out;		/* the resource file to write */
	int		dry;
	int		no_names;
	const char	*names_out;	/* --names-out */
	const char	*in;		/* the input file */
	const char	*names_src;	/* the name file read, or NULL */
	int		dropped;	/* name entries of it that did not match the resource */
};

/* Writes the edited resource and its names as the plan says (after all
 * commands succeeded).  0, or -1 after an error line. */
static int finish(struct rcs_ctx *c, const struct plan *pl)
{
	struct rsc_error	err;
	struct rsc_names	*w = NULL;
	struct stat		st;
	uint8_t			*rbuf = NULL, *nbuf = NULL;
	size_t			rlen = 0, nlen = 0;
	char			*derived = NULL, why[300], first[200], eb[1024], eb2[1024];
	const char		*ntarget = NULL, *target = pl->out ? pl->out : pl->in;
	int			cw, ch, rc = -1, names_existed = 0, names_written = 0, bad, i, cnt;
	struct rsc_name		e;
	struct rsc		*chk;

	if (rsc_validate(c->r, &err) != 0)
	{
		rcs_error("%s: the result is not valid: %s", esc(eb, sizeof eb, target), err.msg);
		return -1;
	}
	rsc_cell(c->r, &cw, &ch);
	if (rsc_atari_write(c->r, cw, ch, &rbuf, &rlen, &err) != 0)
	{
		rcs_error("%s: cannot write the result: %s", esc(eb, sizeof eb, target), err.msg);
		return -1;
	}
	/* the writer numbers the objects of every tree in preorder: the names follow */
	if (preorder_names(c) != 0)
	{
		rcs_error("%s: out of memory", esc(eb, sizeof eb, target));
		goto out;
	}
	/* the library must be able to read what it wrote */
	chk = rsc_atari_read(rbuf, rlen, cw, ch, &err);
	if (!chk)
	{
		rcs_error("%s: the library cannot read the result back (%s); nothing was written",
			  esc(eb, sizeof eb, target), err.msg);
		goto out;
	}
	rsc_free(chk);
	if (!pl->dry && stat(target, &st) == 0 && access(target, W_OK) != 0)
	{
		rcs_error("%s: not writable; nothing was written", esc(eb, sizeof eb, target));
		goto out;
	}
	if (!pl->no_names)
	{
		if (pl->names_out)
			ntarget = pl->names_out;
		else if (pl->out)
			ntarget = derived = with_ext(pl->out, ".dfn");
		else if (pl->names_src && has_ext(pl->names_src, ".dfn"))
			ntarget = pl->names_src;
		else
			ntarget = derived = with_ext(pl->in, ".dfn");
		if (!ntarget)
		{
			rcs_error("out of memory");
			goto out;
		}
		if (!pl->out && !pl->names_out && ntarget == derived && stat(derived, &st) == 0
		    && !(pl->names_src && same_path(pl->names_src, derived)))
		{
			rcs_error("%s: exists, but the names of %s did not come from it; use --names %s, "
				  "--names-out FILE or --no-names", esc(eb, sizeof eb, derived),
				  esc(eb2, sizeof eb2, pl->in), esc(eb2, sizeof eb2, derived));
			goto out;
		}
		if (!pl->dry && stat(ntarget, &st) == 0 && access(ntarget, W_OK) != 0)
		{
			rcs_error("%s: not writable; nothing was written", esc(eb, sizeof eb, ntarget));
			goto out;
		}
		if (pl->dropped > 0 && pl->names_src && stat(ntarget, &st) == 0 && same_path(ntarget, pl->names_src))
			rcs_error("warning: %d entries of %s that did not match the resource are not written "
				  "(the file is replaced)", pl->dropped, esc(eb, sizeof eb, ntarget));
		bad = dfn_misfits(c->n, c->r, first, sizeof first);
		w = c->n;
		if (bad && pl->names_out)
		{
			/* leave out what does not fit */
			w = rsc_names_new();
			cnt = rsc_names_count(c->n);
			for (i = 0; w && i < cnt; i++)
				if (rsc_names_get(c->n, i, &e) == 0 && !(strlen(e.name) > 8)
				    && !(e.kind == RSC_NAME_OBJ && (e.tree > 255 || e.obj > 255))
				    && rsc_names_add(w, e.kind, e.tkind, e.tree, e.obj, e.name, e.guessed) != 0)
				{
					rsc_names_free(w);
					w = NULL;
				}
			if (!w)
			{
				rcs_error("out of memory");
				goto out;
			}
			rcs_error("warning: %d names do not fit a .dfn file and were not written (the first is %s)",
				  bad, first);
		}
		else if (bad)
		{
			rcs_error("%s: %d names do not fit a .dfn file (names have at most 8 bytes, tree and "
				  "object numbers at most 255), the first is %s; nothing was written; use "
				  "--no-names to write no names, or --names-out FILE to write the others",
				  esc(eb, sizeof eb, ntarget), bad, first);
			goto out;
		}
		nlen = (size_t)rsc_names_write_dfn(w, c->r, NULL, 0, &err);
		if ((long)nlen < 0 || (nbuf = malloc(nlen + 1)) == NULL
		    || rsc_names_write_dfn(w, c->r, nbuf, nlen, &err) < 0)
		{
			rcs_error("%s: cannot write the names: %s", esc(eb, sizeof eb, ntarget),
				  (long)nlen < 0 ? err.msg : "out of memory");
			goto out;
		}
		/* no entries and no file there: no file */
		if (nlen <= 2 && stat(ntarget, &st) != 0)
		{
			free(nbuf);
			nbuf = NULL;
		}
	}
	if (pl->dry)
	{
		rcs_note("dry run: nothing written (would write %s, %lu bytes%s%s)", esc(eb, sizeof eb, target),
			 (unsigned long)rlen, nbuf ? ", and the names to " : "", nbuf ? esc(eb2, sizeof eb2, ntarget) : "");
		rc = 0;
		goto out;
	}
	if (nbuf)
	{
		names_existed = stat(ntarget, &st) == 0;
		if (rcs_write_file(ntarget, nbuf, nlen, 1, why, sizeof why) != 0)
		{
			rcs_error("%s: cannot write the names: %s; nothing was written", esc(eb, sizeof eb, ntarget), why);
			goto out;
		}
		names_written = 1;
		if (pl->names_src && !has_ext(pl->names_src, ".dfn") && !pl->names_out && !pl->out)
			rcs_note("the names were read from %s, which is left as it is; they are written to %s",
				 esc(eb, sizeof eb, pl->names_src), esc(eb2, sizeof eb2, ntarget));
	}
	if (rcs_write_file(target, rbuf, rlen, 1, why, sizeof why) != 0)
	{
		const char	*what = "";

		if (names_written)
		{
			if (names_existed)
				what = "; the name file had already been replaced";
			else
			{
				remove(ntarget);
				what = "; the new name file was removed";
			}
		}
		rcs_error("%s: cannot write: %s%s", esc(eb, sizeof eb, target), why, what);
		goto out;
	}
	rc = 0;
out:
	if (w && w != c->n)
		rsc_names_free(w);
	rsc_mfree(rbuf);
	free(nbuf);
	free(derived);
	return rc;
}

int main(int argc, char **argv)
{
	struct rcs_ctx		ctx;
	struct plan		pl;
	const char		*names_file = NULL, *file, *why = "";
	char			*found = NULL, *wbuf = NULL, *words[WORDS_MAX];
	char			eb[1024], eb2[1024];
	int			i, selfcheck = 0, status = 1, nw;

	memset(&ctx, 0, sizeof ctx);
	memset(&pl, 0, sizeof pl);
	memset(&g_opts, 0, sizeof g_opts);
	for (i = 1; i < argc && argv[i][0] == '-' && argv[i][1]; i++)
	{
		const char	*o = argv[i];

		if (strcmp(o, "--help") == 0)
		{
			fputs(usage_text, stdout);
			return 0;
		}
		else if (strcmp(o, "--names") == 0)
		{
			if (i + 1 >= argc)
			{
				rcs_error("--names needs a FILE");
				return 1;
			}
			names_file = argv[++i];
		}
		else if (strncmp(o, "--names=", 8) == 0 && o[8])
			names_file = o + 8;
		else if (strcmp(o, "--no-names") == 0)
			g_opts.no_names = 1;
		else if (strcmp(o, "--no-guess") == 0)
			g_opts.no_guess = 1;
		else if (strcmp(o, "--selfcheck") == 0)
			selfcheck = 1;
		else if (strcmp(o, "-n") == 0)
			pl.dry = 1;
		else if (strcmp(o, "-o") == 0)
		{
			if (i + 1 >= argc)
			{
				rcs_error("-o needs a file name");
				return 1;
			}
			pl.out = argv[++i];
			if (!has_ext(pl.out, ".rsc"))
			{
				rcs_error("-o: %s: output format not supported yet", esc(eb, sizeof eb, pl.out));
				return 1;
			}
		}
		else if (strcmp(o, "--names-out") == 0)
		{
			if (i + 1 >= argc)
			{
				rcs_error("--names-out needs a FILE");
				return 1;
			}
			pl.names_out = argv[++i];
			if (!has_ext(pl.names_out, ".dfn"))
			{
				rcs_error("--names-out: %s: only .dfn name files can be written yet",
					  esc(eb, sizeof eb, pl.names_out));
				return 1;
			}
		}
		else if (strcmp(o, "--pixels") == 0)
		{
			g_opts.cw = 8;
			g_opts.ch = 16;
		}
		else if (strncmp(o, "--pixels=", 9) == 0)
		{
			if (parse_cell(o + 9, &g_opts.cw, &g_opts.ch) != 0)
			{
				rcs_error("bad --pixels value '%s' (WxH, each 1 to %d)", esc(eb, sizeof eb, o + 9),
					  RSC_MAX_CELL);
				return 1;
			}
		}
		else
		{
			rcs_error("unknown option '%s' (try --help)", esc(eb, sizeof eb, o));
			return 1;
		}
	}
	if (names_file && g_opts.no_names)
	{
		rcs_error("--names and --no-names exclude each other");
		return 1;
	}
	if (pl.names_out && g_opts.no_names)
	{
		rcs_error("--names-out and --no-names exclude each other");
		return 1;
	}
	if (pl.dry && pl.out)
	{
		rcs_error("-n and -o exclude each other");
		return 1;
	}
	if (i >= argc)
	{
		rcs_error("missing FILE.rsc (try --help)");
		return 1;
	}
	file = argv[i++];
	if (i >= argc)
	{
		rcs_error("missing COMMAND (try --help)");
		return 1;
	}
	/* check the -c form before reading anything */
	if (strcmp(argv[i], "-c") == 0)
	{
		int	k;

		for (k = i; k < argc; k += 2)
			if (strcmp(argv[k], "-c") != 0 || k + 1 >= argc)
			{
				rcs_error("-c needs a command string, and only -c may follow it");
				return 1;
			}
	}

	if (load_pair(file, names_file, &ctx.r, &ctx.n, &found, &pl.dropped) != 0)
		goto out;
	ctx.v = rcs_view_new(ctx.r, ctx.n);
	if (!ctx.v)
	{
		rcs_error("out of memory");
		goto out;
	}
	ctx.file = file;
	ctx.no_guess = g_opts.no_guess;
	ctx.selfcheck = selfcheck;
	ctx.load = load_other;

	if (strcmp(argv[i], "-c") != 0)
		status = rcs_run(&ctx, argc - i, argv + i);
	else
		for (status = 0; i < argc && status == 0; i += 2)
		{
			wbuf = malloc(strlen(argv[i + 1]) + 1);
			if (!wbuf)
			{
				rcs_error("out of memory");
				status = 1;
				break;
			}
			nw = split(argv[i + 1], wbuf, words, WORDS_MAX, &why);
			if (nw <= 0)
			{
				rcs_error("-c '%s': %s", esc(eb2, sizeof eb2, argv[i + 1]),
					  nw < 0 ? why : "empty command");
				status = 1;
			}
			else
				status = rcs_run(&ctx, nw, words);
			free(wbuf);
			wbuf = NULL;
		}
	if (status == 0 && ctx.edited)
	{
		pl.in = file;
		pl.no_names = g_opts.no_names;
		pl.names_src = found;
		if (finish(&ctx, &pl) != 0)
			status = 1;
	}
	else if (status == 0 && pl.out)
		rcs_note("no edit command ran: %s was not written", esc(eb, sizeof eb, pl.out));
out:
	if (fflush(stdout) != 0 && status == 0)
	{
		rcs_error("cannot write the output: %s", strerror(errno));
		status = 1;
	}
	free(found);
	rcs_ctx_free_others(&ctx);
	rcs_view_free(ctx.v);
	rsc_names_free(ctx.n);
	rsc_free(ctx.r);
	return status;
}
