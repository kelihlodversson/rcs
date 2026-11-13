/* GEMlib's compiler declarations for the pTOS SDK stack/standard ABI. */
#ifndef RCS_COMPAT_COMPILER_H
#define RCS_COMPAT_COMPILER_H

#ifdef __cplusplus
#define __BEGIN_DECLS extern "C" {
#define __END_DECLS }
#else
#define __BEGIN_DECLS
#define __END_DECLS
#endif
#define __CDECL

#endif
