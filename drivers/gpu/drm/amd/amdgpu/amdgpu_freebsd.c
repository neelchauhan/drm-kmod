#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/module.h>

MODULE_DEPEND(amdgpu, drmn, 2, 2, 2);
MODULE_DEPEND(amdgpu, ttm, 1, 1, 1);
#ifdef CONFIG_AGP
MODULE_DEPEND(amdgpu, agp, 1, 1, 1);
#endif
MODULE_DEPEND(amdgpu, linuxkpi, 1, 1, 1);
MODULE_DEPEND(amdgpu, linuxkpi_gplv2, 1, 1, 1);
MODULE_DEPEND(amdgpu, firmware, 1, 1, 1);
#ifdef CONFIG_DEBUG_FS
MODULE_DEPEND(amdgpu, debugfs, 1, 1, 1);
#endif
