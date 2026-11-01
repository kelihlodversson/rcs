/* HOSTSTUBS.C	the GEM, GEMDOS and GUI parts of RCS, on a host without them.
 *
 * Alerts print their text to stderr and answer the first button.  Drawing,
 * mouse and window calls do nothing and return 0.  The pure helpers (object
 * tree links, rectangles, the file name of a resource) are the real ones, so
 * the load and save code sees the trees and names it expects. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rcsaddr.h"
#include "rcshost.h"
#include "rcsdefs.h"
#include "rcsdata.h"
#include "rcsdisk.h"
#include "rcslib.h"
#include "rcsintf.h"
#include "rcsalert.h"
#include "rcsfiles.h"
#include "rcsmono.h"
#include "rcsvdi.h"

#define TRUE	1
#define FALSE	0

/* The VDI handle, and the cell of the system font.  The goldens in tests/golden
 * were made under the pTOS virt-arm image: its screen is the 1280x800 scanout
 * of QEMU's virtio-gpu, and the system font there is 8x16, so a character is 8
 * pixels wide and a line 16 high.  Checked on build/emulator/c2-1.png (ARM):
 * the alert lines are 16 pixels apart and "Can't find definition (.DFN)" (28
 * characters) spans about 222 pixels.  build/emulator/m1-1.png (m68k panic
 * screen) shows the same cell.  ch_width and ch_height must agree with these. */
short gl_handle = 0;
short gl_hchar = 16;
short gl_wchar = 8;

/* the monochrome conversions are only reached from rast_op, which only the
 * GUI calls: if one of them is ever called from the load or save path, stop */
static void not_on_load_save(const char *name)
{
    fprintf(stderr, "%s: not supported on the host\n", name);
    abort();
}

extern char rcs_rfile[];

/* ---- GEMDOS ---------------------------------------------------------- */

/* the disk always has room: the writer checks the space before it writes */
long Dfree(_DISKINFO *buf, short drv)
{
    (void)drv;
    buf->b_free = 1000000;
    buf->b_total = 1000000;
    buf->b_secsiz = 512;
    buf->b_clsiz = 1;
    return 0;
}

/* ---- AES alerts and errors ------------------------------------------- */

/* the first button; the caller of form_error does not retry on the host */
short form_error(short errorcode)
{
    fprintf(stderr, "error: GEMDOS error %d\n", (int)errorcode);
    return FALSE;
}

/* answers the default button, b_num: for the alert of open_files() about a
 * missing definition file that is the button that continues without one
 * (button 1 would cancel the load, and the Atari goldens were made with the
 * load going on) */
short hndl_alert(short b_num, long addr)
{
    if (addr)
        fprintf(stderr, "alert: %s\n", (const char *)RP(addr));
    return b_num;
}

/* The program's own resource (RCS.RSC: the panel and the boxes comp_str() and
 * comp_img() copy, the alert strings) is what rsrc_load() gives the Atari
 * program.  Here rsrc_load() reads that file into the arena and does what the
 * AES does with it: the file's offsets become addresses and the character
 * coordinates become pixels (8x16 cell, see gl_wchar above). */
static raddr own_hdr;

#define OW(off)		(*(unsigned short *)RP(own_hdr + (off)))

static void own_ptr(long a)
{
    if (RGET(a) != -1L)
        RFIELD(a) = own_hdr + RFIELD(a);
}

short rsrc_load(const char *name)
{
    FILE *f = fopen(name, "rb");
    long size, saved_head = head, saved_size = buff_size, saved_free = rcs_free;
    short i, ok;

    if (!f)
        return 0;
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    own_hdr = rcs_alloc(size + 0x1000);
    if (fread(RP(own_hdr), 1, size, f) != (size_t)size) {
        fclose(f);
        return 0;
    }
    fclose(f);
    /* the tables stay where they are (the records have the sizes of the file),
     * so rsc_to_native() needs no room, only rcs_free to say where the file ends */
    head = own_hdr;
    buff_size = size + 0x1000;
    rcs_free = own_hdr + size;
    ok = rsc_to_native(own_hdr, 0L);
    head = saved_head;
    buff_size = saved_size;
    rcs_free = saved_free;
    if (!ok)
        return 0;
    for (i = 0; i < (short)OW(22); i++)
        own_ptr(own_hdr + OW(18) + 4L * i);
    for (i = 0; i < (short)OW(30); i++)
        own_ptr(own_hdr + OW(10) + 4L * i);
    for (i = 0; i < (short)OW(32); i++)
        own_ptr(own_hdr + OW(16) + 4L * i);
    for (i = 0; i < (short)OW(24); i++) {
        long t = own_hdr + OW(4) + 28L * i;

        own_ptr(t);
        own_ptr(t + 4);
        own_ptr(t + 8);
    }
    for (i = 0; i < (short)OW(26); i++) {
        long t = own_hdr + OW(6) + 34L * i;

        own_ptr(t);
        own_ptr(t + 4);
        own_ptr(t + 8);
    }
    for (i = 0; i < (short)OW(28); i++)
        own_ptr(own_hdr + OW(8) + 14L * i);
    for (i = 0; i < (short)OW(20); i++) {
        long o = own_hdr + OW(2) + 24L * i;

        switch (*(unsigned short *)RP(o + 6) & 0xff) {
        case G_TEXT: case G_BOXTEXT: case G_FTEXT: case G_FBOXTEXT:
        case G_BUTTON: case G_STRING: case G_TITLE:
        case G_IMAGE: case G_ICON:
            own_ptr(o + 12);
            break;
        default:
            break;
        }
        rs_obfix(o, 0);
    }
    return 1;
}

short rsrc_gaddr(short type, short index, long *addr)
{
    *addr = 0;
    if (!own_hdr || index < 0)
        return 0;
    switch (type) {
    case R_TREE:      *addr = RGET(own_hdr + OW(18) + 4L * index); break;
    case R_OBJECT:    *addr = own_hdr + OW(2) + 24L * index; break;
    case R_TEDINFO:   *addr = own_hdr + OW(4) + 28L * index; break;
    case R_ICONBLK:   *addr = own_hdr + OW(6) + 34L * index; break;
    case R_BITBLK:    *addr = own_hdr + OW(8) + 14L * index; break;
    case R_STRING:    *addr = RGET(own_hdr + OW(10) + 4L * index); break;
    case R_IMAGEDATA: *addr = RGET(own_hdr + OW(16) + 4L * index); break;
    default:          return 0;
    }
    return 1;
}

short rsrc_free(void)
{
    return 1;
}

void comp_alerts(long hdr)
{
    (void)hdr;
}

void dcomp_alerts(void)
{
}

/* ---- AES forms and files --------------------------------------------- */

/* the file name was given on the command line: keep it */
short get_file(short label)
{
    (void)label;
    return TRUE;
}

/* the name of a resource file with the extension ext */
void r_to_xfile(char *where, char *ext)
{
    short ii, jj;

    strcpy(where, rcs_rfile);
    ii = (jj = (short)strlen(where)) - 1;
    while ((ii && where[ii] != '.') && where[ii] != '\\')
        ii--;
    if (where[ii] != '.')
        where[ii = jj] = '.';
    strcpy(&where[++ii], ext);
}

void ini_prog(void)
{
}

void show_prog(short strno)
{
    (void)strno;
}

void clr_clip(void)
{
}

/* ---- AES objects ----------------------------------------------------- */

/* the children of an object form a ring: the last one points to the parent */
short objc_add(OBJECT *tree, short parent, short child)
{
    tree[child].ob_next = parent;
    if (tree[parent].ob_head == NIL)
        tree[parent].ob_head = child;
    else
        tree[tree[parent].ob_tail].ob_next = child;
    tree[parent].ob_tail = child;
    return 1;
}

/* the parent of obj in the subtree at cur, or NIL */
static short find_parent(OBJECT *tree, short obj, short cur)
{
    short c, r;

    if (tree[cur].ob_head == NIL)
        return NIL;
    for (c = tree[cur].ob_head; ; c = tree[c].ob_next) {
        if (c == obj)
            return cur;
        r = find_parent(tree, obj, c);
        if (r != NIL)
            return r;
        if (c == tree[cur].ob_tail)
            break;
    }
    return NIL;
}

/* the screen position of the upper left corner of obj */
short objc_offset(OBJECT *tree, short obj, short *x, short *y)
{
    short cur = obj, sx = 0, sy = 0;

    for (;;) {
        sx += tree[cur].ob_x;
        sy += tree[cur].ob_y;
        if (cur == ROOT)
            break;
        cur = find_parent(tree, cur, ROOT);
        if (cur == NIL)
            break;
    }
    *x = sx;
    *y = sy;
    return TRUE;
}

short graf_mouse(short form, const void *addr)
{
    (void)form;
    (void)addr;
    return 0;
}

short wind_get(short handle, short field, short *w1, short *w2, short *w3, short *w4)
{
    (void)handle;
    (void)field;
    if (w1) *w1 = 0;
    if (w2) *w2 = 0;
    if (w3) *w3 = 0;
    if (w4) *w4 = 0;
    return 0;
}

short wind_set(short handle, short field, short w1, short w2, short w3, short w4)
{
    (void)handle;
    (void)field;
    (void)w1;
    (void)w2;
    (void)w3;
    (void)w4;
    return 0;
}

/* ---- GEM rectangles -------------------------------------------------- */

void rc_copy(const GRECT *src, GRECT *dst)
{
    *dst = *src;
}

/* dst becomes the intersection of the two rectangles, if they overlap */
short rc_intersect(const GRECT *r1, GRECT *r2)
{
    short tx, ty, tw, th, ret;

    tx = r2->g_x > r1->g_x ? r2->g_x : r1->g_x;
    tw = (r2->g_x + r2->g_w < r1->g_x + r1->g_w ? r2->g_x + r2->g_w : r1->g_x + r1->g_w) - tx;
    ret = (0 < tw);
    if (ret) {
        ty = r2->g_y > r1->g_y ? r2->g_y : r1->g_y;
        th = (r2->g_y + r2->g_h < r1->g_y + r1->g_h ? r2->g_y + r2->g_h : r1->g_y + r1->g_h) - ty;
        ret = (0 < th);
        if (ret) {
            r2->g_x = tx;
            r2->g_y = ty;
            r2->g_w = tw;
            r2->g_h = th;
        }
    }
    return ret;
}

/* the VDI form of a rectangle: x1, y1, x2, y2 with the last pixel inside */
short *grect_to_array(const GRECT *area, short *array)
{
    array[0] = area->g_x;
    array[1] = area->g_y;
    array[2] = area->g_x + area->g_w - 1;
    array[3] = area->g_y + area->g_h - 1;
    return array;
}

/* ---- VDI and the screen ---------------------------------------------- */

/* the width and height of a character of the system font: the cell above */
short ch_width(short fn)
{
    (void)fn;
    return 8;
}

short ch_height(short fn)
{
    (void)fn;
    return 16;
}

void gsx_invert(GRECT *pt)
{
    (void)pt;
}

void gsx_outline(GRECT *pt)
{
    (void)pt;
}

/* gsx_trans and gsx_untrans convert between the standard form of a bit image
 * (one plane of words, as in ICONBLK and BITBLK data) and the device form.
 * With one plane the two forms have the same bits, so the conversion is a copy
 * of h lines of bytes, with the line length in bytes (swb, dwb) as the stride.
 * The real calls always set nplanes to 1, and these signatures carry no plane
 * count, so no plane check is possible here. */
static void copy_image(long saddr, short swb, long daddr, short dwb, short h)
{
    unsigned char *s = RP(saddr);
    unsigned char *d = RP(daddr);
    short y, n = swb < dwb ? swb : dwb;

    for (y = 0; y < h; y++)
        memmove(d + (long)y * dwb, s + (long)y * swb, n);
}

/* standard form to device form: 1 plane, so this is the copy above */
void gsx_trans(long saddr, short swb, long daddr, short dwb, short h)
{
    /* the load path calls this in place (saddr == daddr, swb == dwb): the
     * bits are already in the device form, so there is nothing to do */
    if (saddr == daddr && swb == dwb)
        return;
    copy_image(saddr, swb, daddr, dwb, h);
}

/* device form to standard form: 1 plane, so this is the copy above */
void gsx_untrans(long saddr, short swb, long daddr, short dwb, short h)
{
    copy_image(saddr, swb, daddr, dwb, h);
}

void vro_cpyfm(short handle, short mode, short pxy[], MFDB *src, MFDB *dst)
{
    (void)handle;
    (void)mode;
    (void)pxy;
    (void)src;
    (void)dst;
}

/* ---- the monochrome conversion of the screen (rcsmono.c) ------------- */

short edit_colour(void)
{
    not_on_load_save("edit_colour");
    return 0;
}

short is_mono(const MFDB *m)
{
    (void)m;
    not_on_load_save("is_mono");
    return 0;
}

void mono_rop(short mode, GRECT *sa, MFDB *s, GRECT *da, MFDB *d)
{
    (void)mode;
    (void)sa;
    (void)s;
    (void)da;
    (void)d;
    not_on_load_save("mono_rop");
}

void mono_to_scrn(GRECT *sa, MFDB *s, GRECT *da, short colour)
{
    (void)sa;
    (void)s;
    (void)da;
    (void)colour;
    not_on_load_save("mono_to_scrn");
}

void scrn_to_mono(GRECT *sa, MFDB *d, GRECT *da, short colour)
{
    (void)sa;
    (void)d;
    (void)da;
    (void)colour;
    not_on_load_save("scrn_to_mono");
}
