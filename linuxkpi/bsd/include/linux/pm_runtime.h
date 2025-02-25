/* Public domain. */

#ifndef _LINUX_PM_RUNTIME_H
#define _LINUX_PM_RUNTIME_H

#include <linux/device.h>
#include <linux/pm.h>

#define pm_runtime_mark_last_busy(x)
#define pm_runtime_use_autosuspend(x)
#define pm_runtime_dont_use_autosuspend(x)
#define pm_runtime_put_autosuspend(x)
#define pm_runtime_set_autosuspend_delay(x, y)
#define pm_runtime_set_active(x)
#define pm_runtime_allow(x)
#define pm_runtime_put_noidle(x)
#define pm_runtime_forbid(x)
#define pm_runtime_get_noresume(x)
#define pm_runtime_put(x)
#define pm_runtime_enable(x)
#define pm_runtime_disable(x)
#define pm_runtime_autosuspend(x)

static inline int
pm_runtime_get_sync(struct device *dev)
{
	return 0;
}

static inline int
pm_runtime_get_if_in_use(struct device *dev)
{
	return 1;
}

static inline int
pm_runtime_get_if_active(struct device *dev, bool x)
{
	return -EINVAL;
}

#endif
