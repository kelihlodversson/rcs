/* Adapt GEMlib's packed control words to the native pTOS VDI entry.
 * The application continues to use the standard GEMlib VDI API. */
#define vdi rcs_native_vdi
#include "vdi_user.h"
#undef vdi
#include "gem_vdiP.h"

_Static_assert(sizeof(MFDB) == sizeof(VDI_USER_FDB), "native MFDB layout");

void vdi(VDIPB *pb)
{
    VDI_USER_CONTROL control = { 0 };
    VDI_USER_PB native;

    control.code = pb->control[0];
    control.nptsin = pb->control[1];
    control.nintin = pb->control[3];
    control.subcode = pb->control[5];
    control.handle = pb->control[6];
    if (control.code == 109 || control.code == 110 || control.code == 121) {
        control.args.raster.source = vdi_control_get_ptr(pb->control, 0);
        control.args.raster.destination = vdi_control_get_ptr(pb->control, 1);
    }
    native.contrl = &control;
    native.intin = NO_CONST(pb->intin);
    native.ptsin = NO_CONST(pb->ptsin);
    native.intout = pb->intout;
    native.ptsout = pb->ptsout;
    rcs_native_vdi(&native);
    pb->control[2] = control.nptsout;
    pb->control[4] = control.nintout;
    pb->control[6] = control.handle;
}
