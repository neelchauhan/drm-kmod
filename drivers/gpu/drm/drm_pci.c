/*
 * Copyright 2003 José Fonseca.
 * Copyright 2003 Leif Delgass.
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
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
 * AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <linux/dma-mapping.h>
#include <linux/export.h>
#include <linux/pci.h>
#include <linux/slab.h>

#include <drm/drm.h>
#include <drm/drm_agpsupport.h>
#include <drm/drm_drv.h>
#include <drm/drm_pci.h>
#include <drm/drm_print.h>

#include "drm_internal.h"
#include "drm_legacy.h"


#ifdef __FreeBSD__
#define aper_base ai_aperture_base
#define aper_size ai_aperture_size

static void
drm_pci_busdma_callback(void *arg, bus_dma_segment_t *segs, int nsegs, int error)
{
	drm_dma_handle_t *dmah = arg;

	if (error != 0)
		return;

	KASSERT(nsegs == 1, ("drm_pci_busdma_callback: bad dma segment count"));
	dmah->busaddr = segs[0].ds_addr;
}
#endif

/**
 * drm_pci_alloc - Allocate a PCI consistent memory block, for DMA.
 * @dev: DRM device
 * @size: size of block to allocate
 * @align: alignment of block
 *
 * FIXME: This is a needless abstraction of the Linux dma-api and should be
 * removed.
 *
 * Return: A handle to the allocated memory block on success or NULL on
 * failure.
 */
drm_dma_handle_t *drm_pci_alloc(struct drm_device * dev, size_t size, size_t align)
{
#ifdef __FreeBSD__
	drm_dma_handle_t *dmah;
	int ret;

	/* Need power-of-two alignment, so fail the allocation if it isn't. */
	if ((align & (align - 1)) != 0) {
		DRM_ERROR("drm_pci_alloc with non-power-of-two alignment %d\n",
		    (int)align);
		return NULL;
	}

	dmah = malloc(sizeof(drm_dma_handle_t), DRM_MEM_DMA, M_ZERO | M_NOWAIT);
	if (dmah == NULL)
		return NULL;

	ret = bus_dma_tag_create(
	    bus_get_dma_tag(dev->dev->bsddev), /* parent */
	    align, 0, /* align, boundary */
	    BUS_SPACE_MAXADDR, BUS_SPACE_MAXADDR, /* lowaddr, highaddr */
	    NULL, NULL, /* filtfunc, filtfuncargs */
	    size, 1, size, /* maxsize, nsegs, maxsegsize */
	    0, NULL, NULL, /* flags, lockfunc, lockfuncargs */
	    &dmah->tag);
	if (ret != 0) {
		free(dmah, DRM_MEM_DMA);
		return NULL;
	}

	ret = bus_dmamem_alloc(dmah->tag, (void **)&dmah->vaddr,
	    BUS_DMA_WAITOK | BUS_DMA_ZERO | BUS_DMA_NOCACHE, &dmah->map);
	if (ret != 0) {
		bus_dma_tag_destroy(dmah->tag);
		free(dmah, DRM_MEM_DMA);
		return NULL;
	}

	ret = bus_dmamap_load(dmah->tag, dmah->map, dmah->vaddr, size,
	    drm_pci_busdma_callback, dmah, BUS_DMA_NOWAIT);
	if (ret != 0) {
		bus_dmamem_free(dmah->tag, dmah->vaddr, dmah->map);
		bus_dma_tag_destroy(dmah->tag);
		free(dmah, DRM_MEM_DMA);
		return NULL;
	}
	return dmah;
#else
	drm_dma_handle_t *dmah;

	/* pci_alloc_consistent only guarantees alignment to the smallest
	 * PAGE_SIZE order which is greater than or equal to the requested size.
	 * Return NULL here for now to make sure nobody tries for larger alignment
	 */
	if (align > size)
		return NULL;

	dmah = kmalloc(sizeof(drm_dma_handle_t), GFP_KERNEL);
	if (!dmah)
		return NULL;

	dmah->size = size;
	dmah->vaddr = dma_alloc_coherent(&dev->pdev->dev, size,
					 &dmah->busaddr,
					 GFP_KERNEL);

	if (dmah->vaddr == NULL) {
		kfree(dmah);
		return NULL;
	}

	return dmah;
#endif
}
EXPORT_SYMBOL(drm_pci_alloc);

/**
 * drm_pci_free - Free a PCI consistent memory block
 * @dev: DRM device
 * @dmah: handle to memory block
 *
 * FIXME: This is a needless abstraction of the Linux dma-api and should be
 * removed.
 */
void drm_pci_free(struct drm_device * dev, drm_dma_handle_t * dmah)
{
	dma_free_coherent(&dev->pdev->dev, dmah->size, dmah->vaddr,
			  dmah->busaddr);
	kfree(dmah);
}

EXPORT_SYMBOL(drm_pci_free);

static int drm_get_pci_domain(struct drm_device *dev)
{
#ifndef __alpha__
	/* For historical reasons, drm_get_pci_domain() is busticated
	 * on most archs and has to remain so for userspace interface
	 * < 1.4, except on alpha which was right from the beginning
	 */
	if (dev->if_version < 0x10004)
		return 0;
#endif /* __alpha__ */

#ifdef __FreeBSD__
	return pci_get_domain(dev->dev->bsddev);
#else
	return pci_domain_nr(dev->pdev->bus);
#endif
}

int drm_pci_set_busid(struct drm_device *dev, struct drm_master *master)
{
#ifdef __FreeBSD__
	master->unique = kasprintf(GFP_KERNEL, "pci:%04x:%02x:%02x.%d",
					drm_get_pci_domain(dev),
					pci_get_bus(dev->dev->bsddev),
					pci_get_slot(dev->dev->bsddev),
					PCI_FUNC(dev->pdev->devfn));
#else
	master->unique = kasprintf(GFP_KERNEL, "pci:%04x:%02x:%02x.%d",
					drm_get_pci_domain(dev),
					dev->pdev->bus->number,
					PCI_SLOT(dev->pdev->devfn),
					PCI_FUNC(dev->pdev->devfn));
#endif
	if (!master->unique)
		return -ENOMEM;

	master->unique_len = strlen(master->unique);
	return 0;
}

static int drm_pci_irq_by_busid(struct drm_device *dev, struct drm_irq_busid *p)
{
	if ((p->busnum >> 8) != drm_get_pci_domain(dev) ||
	    (p->busnum & 0xff) != dev->pdev->bus->number ||
	    p->devnum != PCI_SLOT(dev->pdev->devfn) || p->funcnum != PCI_FUNC(dev->pdev->devfn))
		return -EINVAL;

	p->irq = dev->pdev->irq;

	DRM_DEBUG("%d:%d:%d => IRQ %d\n", p->busnum, p->devnum, p->funcnum,
		  p->irq);
	return 0;
}

/**
 * drm_irq_by_busid - Get interrupt from bus ID
 * @dev: DRM device
 * @data: IOCTL parameter pointing to a drm_irq_busid structure
 * @file_priv: DRM file private.
 *
 * Finds the PCI device with the specified bus id and gets its IRQ number.
 * This IOCTL is deprecated, and will now return EINVAL for any busid not equal
 * to that of the device that this DRM instance attached to.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int drm_irq_by_busid(struct drm_device *dev, void *data,
		     struct drm_file *file_priv)
{
	struct drm_irq_busid *p = data;

	if (!drm_core_check_feature(dev, DRIVER_LEGACY))
		return -EOPNOTSUPP;

	/* UMS was only ever support on PCI devices. */
	if (WARN_ON(!dev->pdev))
		return -EINVAL;

	if (!drm_core_check_feature(dev, DRIVER_HAVE_IRQ))
		return -EOPNOTSUPP;

	return drm_pci_irq_by_busid(dev, p);
}

#ifdef __FreeBSD__
int
drm_getpciinfo(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	struct drm_pciinfo *info = data;

	info->domain = pci_get_domain(dev->dev->bsddev);
	info->bus = pci_get_bus(dev->dev->bsddev);
	info->dev = pci_get_slot(dev->dev->bsddev);
	info->func = pci_get_function(dev->dev->bsddev);
	info->vendor_id = pci_get_vendor(dev->dev->bsddev);
	info->device_id = pci_get_device(dev->dev->bsddev);
	info->subvendor_id = pci_get_subvendor(dev->dev->bsddev);
	info->subdevice_id = pci_get_subdevice(dev->dev->bsddev);
	info->revision_id = pci_get_revid(dev->dev->bsddev);

	return 0;
}
#endif

void drm_pci_agp_destroy(struct drm_device *dev)
{
	if (dev->agp) {
		arch_phys_wc_del(dev->agp->agp_mtrr);
		drm_legacy_agp_clear(dev);
		kfree(dev->agp);
		dev->agp = NULL;
	}
}

#ifdef CONFIG_DRM_LEGACY

static void drm_pci_agp_init(struct drm_device *dev)
{
	if (drm_core_check_feature(dev, DRIVER_USE_AGP)) {
		if (pci_find_capability(dev->pdev, PCI_CAP_ID_AGP))
			dev->agp = drm_agp_init(dev);
		if (dev->agp) {
			dev->agp->agp_mtrr = arch_phys_wc_add(
				dev->agp->agp_info.aper_base,
				dev->agp->agp_info.aper_size *
				1024 * 1024);
		}
	}
}

static int drm_get_pci_dev(struct pci_dev *pdev,
			   const struct pci_device_id *ent,
			   struct drm_driver *driver)
{
	struct drm_device *dev;
	int ret;

	DRM_DEBUG("\n");

	dev = drm_dev_alloc(driver, &pdev->dev);
	if (IS_ERR(dev))
		return PTR_ERR(dev);

	ret = pci_enable_device(pdev);
	if (ret)
		goto err_free;

	dev->pdev = pdev;
#ifdef __alpha__
	dev->hose = pdev->sysdata;
#endif

	if (drm_core_check_feature(dev, DRIVER_MODESET))
		pci_set_drvdata(pdev, dev);

	drm_pci_agp_init(dev);

	ret = drm_dev_register(dev, ent->driver_data);
	if (ret)
		goto err_agp;

	/* No locking needed since shadow-attach is single-threaded since it may
	 * only be called from the per-driver module init hook. */
	if (drm_core_check_feature(dev, DRIVER_LEGACY))
		list_add_tail(&dev->legacy_dev_list, &driver->legacy_dev_list);

	return 0;

err_agp:
	drm_pci_agp_destroy(dev);
	pci_disable_device(pdev);
err_free:
	drm_dev_put(dev);
	return ret;
}

/**
 * drm_legacy_pci_init - shadow-attach a legacy DRM PCI driver
 * @driver: DRM device driver
 * @pdriver: PCI device driver
 *
 * This is only used by legacy dri1 drivers and deprecated.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int drm_legacy_pci_init(struct drm_driver *driver, struct pci_driver *pdriver)
{
	struct pci_dev *pdev = NULL;
	const struct pci_device_id *pid;
	int i;

	DRM_DEBUG("\n");

	if (WARN_ON(!(driver->driver_features & DRIVER_LEGACY)))
		return -EINVAL;

#ifdef __FreeBSD__

	// XXX: clean up this
	
	pdriver->bsdclass = drm_devclass;
	pdriver->name = "drmn";

	if (!(driver->driver_features & DRIVER_LEGACY))
		return linux_pci_register_drm_driver(pdriver);

	DRM_ERROR("FreeBSD needs DRIVER_MODESET");
	return (-ENOTSUP);

#else
	/* If not using KMS, fall back to stealth mode manual scanning. */
	INIT_LIST_HEAD(&driver->legacy_dev_list);
	for (i = 0; pdriver->id_table[i].vendor != 0; i++) {
		pid = &pdriver->id_table[i];

		/* Loop around setting up a DRM device for each PCI device
		 * matching our ID and device class.  If we had the internal
		 * function that pci_get_subsys and pci_get_class used, we'd
		 * be able to just pass pid in instead of doing a two-stage
		 * thing.
		 */
		pdev = NULL;
		while ((pdev =
			pci_get_subsys(pid->vendor, pid->device, pid->subvendor,
				       pid->subdevice, pdev)) != NULL) {
			if ((pdev->class & pid->class_mask) != pid->class)
				continue;

			/* stealth mode requires a manual probe */
			pci_dev_get(pdev);
			drm_get_pci_dev(pdev, pid, driver);
		}
	}
	return 0;
#endif
}
EXPORT_SYMBOL(drm_legacy_pci_init);

/**
 * drm_legacy_pci_exit - unregister shadow-attach legacy DRM driver
 * @driver: DRM device driver
 * @pdriver: PCI device driver
 *
 * Unregister a DRM driver shadow-attached through drm_legacy_pci_init(). This
 * is deprecated and only used by dri1 drivers.
 */
void drm_legacy_pci_exit(struct drm_driver *driver, struct pci_driver *pdriver)
{
	struct drm_device *dev, *tmp;
	DRM_DEBUG("\n");

	if (!(driver->driver_features & DRIVER_LEGACY)) {
		WARN_ON(1);
	} else {
		list_for_each_entry_safe(dev, tmp, &driver->legacy_dev_list,
					 legacy_dev_list) {
			list_del(&dev->legacy_dev_list);
			drm_put_dev(dev);
		}
	}
	DRM_INFO("Module unloaded\n");
}
EXPORT_SYMBOL(drm_legacy_pci_exit);

#endif
