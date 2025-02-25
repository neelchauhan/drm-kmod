/*
 * \file drm_memory.c
 * Memory management wrappers for DRM
 *
 * \author Rickard E. (Rik) Faith <faith@valinux.com>
 * \author Gareth Hughes <gareth@valinux.com>
 */

/*
 * Created: Thu Feb  4 14:00:34 1999 by faith@valinux.com
 *
 * Copyright 1999 Precision Insight, Inc., Cedar Park, Texas.
 * Copyright 2000 VA Linux Systems, Inc., Sunnyvale, California.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * VA LINUX SYSTEMS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include <linux/export.h>
#include <linux/highmem.h>
#include <linux/pci.h>
#include <linux/vmalloc.h>
#ifdef __linux__
#include <xen/xen.h>
#endif

#include <drm/drm_agpsupport.h>
#include <drm/drm_cache.h>
#include <drm/drm_device.h>

#include "drm_legacy.h"

#if IS_ENABLED(CONFIG_AGP)

#ifdef HAVE_PAGE_AGP
# include <asm/agp.h>
#else
# ifdef __powerpc__
#  define PAGE_AGP	pgprot_noncached_wc(PAGE_KERNEL)
# else
#  define PAGE_AGP	PAGE_KERNEL
# endif
#endif

static void *agp_remap(unsigned long offset, unsigned long size,
		       struct drm_device *dev)
{
#ifdef __linux__
	unsigned long i, num_pages =
	    PAGE_ALIGN(size) / PAGE_SIZE;
	struct drm_agp_mem *agpmem;
	struct page **page_map;
	struct page **phys_page_map;
	void *addr;

	size = PAGE_ALIGN(size);

#ifdef __alpha__
	offset -= dev->hose->mem_space->start;
#endif

	list_for_each_entry(agpmem, &dev->agp->memory, head)
		if (agpmem->bound <= offset
		    && (agpmem->bound + (agpmem->pages << PAGE_SHIFT)) >=
		    (offset + size))
			break;
	if (&agpmem->head == &dev->agp->memory)
		return NULL;

	/*
	 * OK, we're mapping AGP space on a chipset/platform on which memory accesses by
	 * the CPU do not get remapped by the GART.  We fix this by using the kernel's
	 * page-table instead (that's probably faster anyhow...).
	 */
	/* note: use vmalloc() because num_pages could be large... */
	page_map = vmalloc(array_size(num_pages, sizeof(struct page *)));
	if (!page_map)
		return NULL;

	phys_page_map = (agpmem->memory->pages + (offset - agpmem->bound) / PAGE_SIZE);
	for (i = 0; i < num_pages; ++i)
		page_map[i] = phys_page_map[i];
	addr = vmap(page_map, num_pages, VM_IOREMAP, PAGE_AGP);
	vfree(page_map);

	return addr;
#elif defined(__FreeBSD__)
	/*
	 * FIXME Linux<->FreeBSD: Not implemented. This is never called
	 * on FreeBSD anyway, because drm_agp_mem->cant_use_aperture is
	 * set to 0.
	 */
	return NULL;
#endif
}

#ifdef __FreeBSD__
#define	vunmap(handle)
#endif
/** Wrapper around agp_free_memory() */
#ifdef __linux__
void drm_free_agp(struct agp_memory *handle, int pages)
#elif defined(__FreeBSD__)
void drm_free_agp(DRM_AGP_MEM * handle, int pages)
#endif
{
	device_t agpdev;

	agpdev = agp_find_device();
	if (!agpdev || !handle)
		return;

#ifdef __linux__
	agp_free_memory(handle);
#elif defined(__FreeBSD__)
	agp_free_memory(agpdev, handle);
#endif
}

/** Wrapper around agp_bind_memory() */
#ifdef __linux__
int drm_bind_agp(struct agp_memory *handle, unsigned int start)
#elif defined(__FreeBSD__)
int drm_bind_agp(DRM_AGP_MEM * handle, unsigned int start)
#endif
{
	device_t agpdev;

	agpdev = agp_find_device();
	if (!agpdev || !handle)
		return -EINVAL;

#ifdef __linux__
	return agp_bind_memory(handle, start);
#elif defined(__FreeBSD__)
	return -agp_bind_memory(agpdev, handle, start * PAGE_SIZE);
#endif
}

/** Wrapper around agp_unbind_memory() */
#ifdef __linux__
int drm_unbind_agp(struct agp_memory *handle)
#elif defined(__FreeBSD__)
int drm_unbind_agp(DRM_AGP_MEM * handle)
#endif
{
	device_t agpdev;

	agpdev = agp_find_device();
	if (!agpdev || !handle)
		return -EINVAL;

#ifdef __linux__
	return agp_unbind_memory(handle);
#elif defined(__FreeBSD__)
	return -agp_unbind_memory(agpdev, handle);
#endif
}

#else /*  CONFIG_AGP  */
static inline void *agp_remap(unsigned long offset, unsigned long size,
			      struct drm_device *dev)
{
	return NULL;
}

#endif /* CONFIG_AGP */

void drm_legacy_ioremap(struct drm_local_map *map, struct drm_device *dev)
{
	if (dev->agp && dev->agp->cant_use_aperture && map->type == _DRM_AGP)
		map->handle = agp_remap(map->offset, map->size, dev);
	else
		map->handle = ioremap(map->offset, map->size);
}
EXPORT_SYMBOL(drm_legacy_ioremap);

void drm_legacy_ioremap_wc(struct drm_local_map *map, struct drm_device *dev)
{
	if (dev->agp && dev->agp->cant_use_aperture && map->type == _DRM_AGP)
		map->handle = agp_remap(map->offset, map->size, dev);
	else
		map->handle = ioremap_wc(map->offset, map->size);
}
EXPORT_SYMBOL(drm_legacy_ioremap_wc);

void drm_legacy_ioremapfree(struct drm_local_map *map, struct drm_device *dev)
{
	if (!map->handle || !map->size)
		return;

	if (dev->agp && dev->agp->cant_use_aperture && map->type == _DRM_AGP)
		vunmap(map->handle);
	else
		iounmap(map->handle);
}
EXPORT_SYMBOL(drm_legacy_ioremapfree);

bool drm_need_swiotlb(int dma_bits)
{
#ifdef __linux__
	struct resource *tmp;
	resource_size_t max_iomem = 0;

	/*
	 * Xen paravirtual hosts require swiotlb regardless of requested dma
	 * transfer size.
	 *
	 * NOTE: Really, what it requires is use of the dma_alloc_coherent
	 *       allocator used in ttm_dma_populate() instead of
	 *       ttm_populate_and_map_pages(), which bounce buffers so much in
	 *       Xen it leads to swiotlb buffer exhaustion.
	 */
	if (xen_pv_domain())
		return true;

	/*
	 * Enforce dma_alloc_coherent when memory encryption is active as well
	 * for the same reasons as for Xen paravirtual hosts.
	 */
	if (mem_encrypt_active())
		return true;

	for (tmp = iomem_resource.child; tmp; tmp = tmp->sibling) {
		max_iomem = max(max_iomem,  tmp->end);
	}

	return max_iomem > ((u64)1 << dma_bits);
#elif defined(__FreeBSD__)
	// Only used in combination with CONFIG_SWIOTLB in v4.17
	// BSDFIXME: Let's say we can dma all physical memory...
	return false;
#endif
}
EXPORT_SYMBOL(drm_need_swiotlb);
