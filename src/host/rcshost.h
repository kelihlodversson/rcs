/* RCSHOST.H	the AES and GEMDOS types RCS uses, for a 64 bit host.
 *
 * The records have the sizes they have on the Atari (OBJECT 24, TEDINFO 28,
 * ICONBLK 34, BITBLK 14 bytes): 16 bit fields, and pointers as 32 bit values
 * that hold an offset into the arena (see rcsaddr.h), not a host pointer. */
#ifndef RCSHOST_H
#define RCSHOST_H

#include <stdint.h>
#include <stdbool.h>

#pragma pack(push, 2)

typedef struct object {
    int16_t ob_next;
    int16_t ob_head;
    int16_t ob_tail;
    uint16_t ob_type;
    uint16_t ob_flags;
    uint16_t ob_state;
    uint32_t ob_spec;
    int16_t ob_x;
    int16_t ob_y;
    int16_t ob_width;
    int16_t ob_height;
} OBJECT;

typedef struct text_edinfo {
    uint32_t te_ptext;
    uint32_t te_ptmplt;
    uint32_t te_pvalid;
    int16_t te_font;
    int16_t te_fontid;
    int16_t te_just;
    int16_t te_color;
    int16_t te_fontsize;
    int16_t te_thickness;
    int16_t te_txtlen;
    int16_t te_tmplen;
} TEDINFO;

typedef struct icon_block {
    uint32_t ib_pmask;
    uint32_t ib_pdata;
    uint32_t ib_ptext;
    int16_t ib_char;
    int16_t ib_xchar;
    int16_t ib_ychar;
    int16_t ib_xicon;
    int16_t ib_yicon;
    int16_t ib_wicon;
    int16_t ib_hicon;
    int16_t ib_xtext;
    int16_t ib_ytext;
    int16_t ib_wtext;
    int16_t ib_htext;
} ICONBLK;

typedef struct bit_block {
    uint32_t bi_pdata;
    int16_t bi_wb;
    int16_t bi_hl;
    int16_t bi_x;
    int16_t bi_y;
    int16_t bi_color;
} BITBLK;

typedef struct graphic_rectangle {
    int16_t g_x;
    int16_t g_y;
    int16_t g_w;
    int16_t g_h;
} GRECT;

typedef struct memory_form {
    uint32_t fd_addr;
    int16_t fd_w;
    int16_t fd_h;
    int16_t fd_wdwidth;
    int16_t fd_stand;
    int16_t fd_nplanes;
    int16_t fd_r1;
    int16_t fd_r2;
    int16_t fd_r3;
} MFDB;

typedef struct point_coord {
    int16_t p_x;
    int16_t p_y;
} PXY;

/* the GEMDOS disk information record (Dfree) */
typedef struct {
    int32_t b_free;
    int32_t b_total;
    int32_t b_secsiz;
    int32_t b_clsiz;
} _DISKINFO;

#pragma pack(pop)

/* the resource file header, as it is on disk (36 bytes) */
typedef struct rshdr {
    int16_t rsh_vrsn;
    uint16_t rsh_object;
    uint16_t rsh_tedinfo;
    uint16_t rsh_iconblk;
    uint16_t rsh_bitblk;
    uint16_t rsh_frstr;
    uint16_t rsh_string;
    uint16_t rsh_imdata;
    uint16_t rsh_frimg;
    uint16_t rsh_trindex;
    int16_t rsh_nobs;
    int16_t rsh_ntree;
    int16_t rsh_nted;
    int16_t rsh_nib;
    int16_t rsh_nbb;
    int16_t rsh_nstring;
    int16_t rsh_nimages;
    uint16_t rsh_rssize;
} RSHDR;
_Static_assert(sizeof(RSHDR) == 36, "RSHDR");

_Static_assert(sizeof(OBJECT) == 24, "OBJECT");
_Static_assert(sizeof(TEDINFO) == 28, "TEDINFO");
_Static_assert(sizeof(ICONBLK) == 34, "ICONBLK");
_Static_assert(sizeof(BITBLK) == 14, "BITBLK");
_Static_assert(sizeof(GRECT) == 8, "GRECT");
_Static_assert(sizeof(MFDB) == 20, "MFDB");


/* rsrc_gaddr types */
#define R_TREE		0
#define R_OBJECT	1
#define R_TEDINFO	2
#define R_ICONBLK	3
#define R_BITBLK	4
#define R_STRING	5
#define R_IMAGEDATA	6
#define R_OBSPEC	7
#define R_TEPTEXT	8
#define R_TEPTMPLT	9
#define R_TEPVALID	10
#define R_IBPMASK	11
#define R_IBPDATA	12
#define R_IBPTEXT	13
#define R_BIPDATA	14
#define R_FRSTR		15
#define R_FRIMG		16

/* wind_get and wind_set fields */
#define WF_HSLIDE	8
#define WF_VSLIDE	9
#define WF_TOP		10
#define WF_HSLSIZE	15
#define WF_VSLSIZE	16

/* graf_mouse forms */
#define M_OFF		256
#define M_ON		257

/* the index of the root object of a tree, and object flags with a
 * GEM 1 name */
#define ROOT		0
#define SUBMENU		0x0800
#define WHITEBAK	0x0040
#define DRAW3D		0x0080

/* the AES and VDI calls, single-threaded, as RCS calls them */
extern short form_error(short errorcode);
extern short form_alert(short button, const char *text);
extern short wind_get(short handle, short field, short *w1, short *w2, short *w3, short *w4);
extern short wind_set(short handle, short field, short w1, short w2, short w3, short w4);
extern short rsrc_gaddr(short type, short index, long *addr);
extern short rsrc_load(const char *name);
extern short rsrc_free(void);
extern short rc_intersect(const GRECT *src, GRECT *dst);
extern void rc_copy(const GRECT *src, GRECT *dst);
extern short *grect_to_array(const GRECT *area, short *array);
extern short objc_add(OBJECT *tree, short parent, short child);
extern short objc_offset(OBJECT *tree, short obj, short *x, short *y);
extern short graf_mouse(short form, const void *addr);
extern void vro_cpyfm(short handle, short mode, short pxy[], MFDB *src, MFDB *dst);

/* object types */
#define G_BOX		20
#define G_TEXT		21
#define G_BOXTEXT	22
#define G_IMAGE		23
#define G_USERDEF	24
#define G_IBOX		25
#define G_BUTTON	26
#define G_BOXCHAR	27
#define G_STRING	28
#define G_FTEXT		29
#define G_FBOXTEXT	30
#define G_ICON		31
#define G_TITLE		32
#define G_CICON		33

/* object flags */
#define NONE		0x0000
#define SELECTABLE	0x0001
#define DEFAULT		0x0002
#define EXIT		0x0004
#define EDITABLE	0x0008
#define RBUTTON		0x0010
#define LASTOB		0x0020
#define TOUCHEXIT	0x0040
#define HIDETREE	0x0080
#define INDIRECT	0x0100

/* object states */
#define NORMAL		0x0000
#define SELECTED	0x0001
#define CROSSED		0x0002
#define CHECKED		0x0004
#define DISABLED	0x0008
#define OUTLINED	0x0010
#define SHADOWED	0x0020

/* colours */
#define WHITE		0
#define BLACK		1
#define RED		2
#define GREEN		3
#define BLUE		4
#define CYAN		5
#define YELLOW		6
#define MAGENTA		7
#define LWHITE		8
#define LBLACK		9
#define LRED		10
#define LGREEN		11
#define LBLUE		12
#define LCYAN		13
#define LYELLOW		14
#define LMAGENTA	15

/* text justification and editing */
#define TE_LEFT		0
#define TE_RIGHT	1
#define TE_CNTR		2
#define EDSTART		0
#define EDINIT		1
#define EDCHAR		2
#define EDEND		3

#define NIL		(-1)

/* GEMDOS: free space of a drive, in hoststubs.c */
extern long Dfree(_DISKINFO *buf, short drv);

#endif
