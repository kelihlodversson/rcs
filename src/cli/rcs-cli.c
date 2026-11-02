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
	"  --help           this text\n"
	"\n"
	"commands:\n"
	"  ls [-l] [-R] [PATH]   list the children of PATH (default /)\n"
	"  tree [PATH]           draw the subtree of PATH\n"
	"  stat PATH             every property of a node\n"
	"  cat [-v] PATH         the text of a node\n"
	"  convert -o OUT.rsc    write the resource with the library's writer\n";

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

int main(int argc, char **argv)
{
	struct rcs_ctx		ctx = { NULL, NULL, NULL };
	struct rsc_error	err;
	const char		*names_file = NULL, *file, *why = "";
	char			*found = NULL, *wbuf = NULL, *words[WORDS_MAX];
	char			eb[1024], eb2[1024];
	uint8_t			*buf = NULL;
	size_t			len = 0;
	int			i, no_names = 0, no_guess = 0, cw = 0, ch = 0, status = 1;
	int			dropped = 0, nw;

	for (i = 1; i < argc && argv[i][0] == '-' && argv[i][1] == '-'; i++)
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
			no_names = 1;
		else if (strcmp(o, "--no-guess") == 0)
			no_guess = 1;
		else if (strcmp(o, "--pixels") == 0)
		{
			cw = 8;
			ch = 16;
		}
		else if (strncmp(o, "--pixels=", 9) == 0)
		{
			if (parse_cell(o + 9, &cw, &ch) != 0)
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
	if (i < argc && argv[i][0] == '-' && argv[i][1])
	{
		rcs_error("unknown option '%s' (try --help)", esc(eb, sizeof eb, argv[i]));
		return 1;
	}
	if (names_file && no_names)
	{
		rcs_error("--names and --no-names exclude each other");
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

	if (load(file, RSC_FILE_MAX, &buf, &len) != 0)
		goto out;
	ctx.r = rsc_atari_read(buf, len, cw, ch, &err);
	free(buf);
	buf = NULL;
	if (!ctx.r)
	{
		rcs_error("%s: %s", esc(eb, sizeof eb, file), err.msg);
		goto out;
	}

	if (!names_file && !no_names)
		names_file = found = find_names(file);
	if (names_file)
	{
		if (load(names_file, NAMES_FILE_MAX, &buf, &len) != 0)
			goto out;
		ctx.n = rsc_names_read_by_ext(names_file, buf, len, &err);
		free(buf);
		buf = NULL;
		if (!ctx.n)
		{
			rcs_error("%s: %s", esc(eb, sizeof eb, names_file), err.msg);
			goto out;
		}
		if (rsc_names_check(ctx.n, ctx.r, &dropped) != 0)
		{
			rcs_error("%s: cannot check the names", esc(eb, sizeof eb, names_file));
			goto out;
		}
		if (dropped)
			rcs_error("warning: %d name entries do not match the resource and were ignored", dropped);
	}
	if (!no_guess)
	{
		if (!ctx.n)
			ctx.n = rsc_names_new();
		if (!ctx.n || rsc_names_guess(ctx.n, ctx.r) < 0)
		{
			rcs_error("%s: cannot guess the names", esc(eb, sizeof eb, file));
			goto out;
		}
	}
	ctx.v = rcs_view_new(ctx.r, ctx.n);
	if (!ctx.v)
	{
		rcs_error("out of memory");
		goto out;
	}

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
	(void)eb2;
out:
	if (fflush(stdout) != 0 && status == 0)
	{
		rcs_error("cannot write the output: %s", strerror(errno));
		status = 1;
	}
	free(buf);
	free(found);
	rcs_view_free(ctx.v);
	rsc_names_free(ctx.n);
	rsc_free(ctx.r);
	return status;
}
