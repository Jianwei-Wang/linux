#include "qxl_drv.h"
#include "qxl_drm.h"
#include "qxl_object.h"

static void qxl_ttm_bo_destroy(struct ttm_buffer_object *tbo)
{
	struct qxl_bo *bo;

	bo = container_of(tbo, struct qxl_bo, tbo);
	mutex_lock(&bo->qdev->gem.mutex);
	list_del_init(&bo->list);
	mutex_unlock(&bo->qdev->gem.mutex);
	kfree(bo);
}

bool qxl_ttm_bo_is_qxl_bo(struct ttm_buffer_object *bo)
{
	if (bo->destroy == &qxl_ttm_bo_destroy)
		return true;
	return false;
}

void qxl_ttm_placement_from_domain(struct qxl_bo *qbo, u32 domain)
{
	u32 c = 0;

	qbo->placement.fpfn = 0;
	qbo->placement.lpfn = 0;
	qbo->placement.placement = qbo->placements;
	qbo->placement.busy_placement = qbo->placements;
	if (domain & QXL_GEM_DOMAIN_VRAM)
		qbo->placements[c++] = TTM_PL_FLAG_CACHED | TTM_PL_FLAG_VRAM;
	if (domain & QXL_GEM_DOMAIN_SURFACE)
		qbo->placements[c++] = TTM_PL_FLAG_CACHED | TTM_PL_FLAG_PRIV0;
	if (domain & QXL_GEM_DOMAIN_CPU)
		qbo->placements[c++] = TTM_PL_MASK_CACHING | TTM_PL_FLAG_SYSTEM;
	if (!c)
		qbo->placements[c++] = TTM_PL_MASK_CACHING | TTM_PL_FLAG_SYSTEM;
	qbo->placement.num_placement = c;
	qbo->placement.num_busy_placement = c;
}


int qxl_bo_create(struct qxl_device *qdev,
		  unsigned long size, bool kernel, u32 domain,
		  struct qxl_bo **bo_ptr)
{
	struct qxl_bo *bo;
	enum ttm_bo_type type;
	int r;
	static int max_size;

	if (unlikely(qdev->mman.bdev.dev_mapping == NULL))
		qdev->mman.bdev.dev_mapping = qdev->ddev->dev_mapping;
	if (kernel)
		type = ttm_bo_type_kernel;
	else
		type = ttm_bo_type_device;
	*bo_ptr = NULL;
	bo = kzalloc(sizeof(struct qxl_bo), GFP_KERNEL);
	if (bo == NULL)
		return -ENOMEM;
	if ((size & (PAGE_SIZE - 1)) != 0) {
		if (size > PAGE_SIZE)
			size = (size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
		else {
			printk_once("lost %ld bytes (%ld -> %ld)\n",
				PAGE_SIZE - size, size, PAGE_SIZE);
			size = PAGE_SIZE;
		}
	}
	if (size > max_size) {
		max_size = size;
		DRM_INFO("%s: max_size = %d\n", __func__, max_size);
	}
	r = drm_gem_object_init(qdev->ddev, &bo->gem_base, size);
	if (unlikely(r)) {
		kfree(bo);
		return r;
	}
	bo->gem_base.driver_private = NULL;
	bo->qdev = qdev;
	bo->surface_reg = -1;
	INIT_LIST_HEAD(&bo->list);

	qxl_ttm_placement_from_domain(bo, domain);
	/* Kernel allocation are uninterruptible */
	/* TODO radeon has a vram_mutex here */
	r = ttm_bo_init(&qdev->mman.bdev, &bo->tbo, size, type,
			&bo->placement, 0, 0, !kernel, NULL, size,
			NULL, &qxl_ttm_bo_destroy);
	if (unlikely(r != 0)) {
		if (r != -ERESTARTSYS)
			dev_err(qdev->dev,
				"object_init failed for (%lu, 0x%08X)\n",
				size, domain);
		return r;
	}
	*bo_ptr = bo;
	return 0;
}

int qxl_bo_kmap(struct qxl_bo *bo, void **ptr)
{
	bool is_iomem;
	int r;

	if (bo->kptr) {
		if (ptr)
			*ptr = bo->kptr;
		return 0;
	}
	r = ttm_bo_kmap(&bo->tbo, 0, bo->tbo.num_pages, &bo->kmap);
	if (r)
		return r;
	bo->kptr = ttm_kmap_obj_virtual(&bo->kmap, &is_iomem);
	if (ptr)
		*ptr = bo->kptr;
	return 0;
}

void qxl_bo_kunmap(struct qxl_bo *bo)
{
	if (bo->kptr == NULL)
		return;
	bo->kptr = NULL;
	ttm_bo_kunmap(&bo->kmap);
}

void qxl_bo_unref(struct qxl_bo **bo)
{
	struct ttm_buffer_object *tbo;

	if ((*bo) == NULL)
		return;
	tbo = &((*bo)->tbo);
	ttm_bo_unref(&tbo);
	if (tbo == NULL)
		*bo = NULL;
}

int qxl_bo_pin(struct qxl_bo *bo, u32 domain, u64 *gpu_addr)
{
	int r, i;

	if (bo->pin_count) {
		bo->pin_count++;
		if (gpu_addr)
			*gpu_addr = qxl_bo_gpu_offset(bo);
		return 0;
	}
	qxl_ttm_placement_from_domain(bo, domain);
	for (i = 0; i < bo->placement.num_placement; i++)
		bo->placements[i] |= TTM_PL_FLAG_NO_EVICT;
	r = ttm_bo_validate(&bo->tbo, &bo->placement, false, false, false);
	if (likely(r == 0)) {
		bo->pin_count = 1;
		if (gpu_addr != NULL)
			*gpu_addr = qxl_bo_gpu_offset(bo);
	}
	if (unlikely(r != 0))
		dev_err(bo->qdev->dev, "%p pin failed\n", bo);
	return r;
}

int qxl_bo_unpin(struct qxl_bo *bo)
{
	int r, i;

	if (!bo->pin_count) {
		dev_warn(bo->qdev->dev, "%p unpin not necessary\n", bo);
		return 0;
	}
	bo->pin_count--;
	if (bo->pin_count)
		return 0;
	for (i = 0; i < bo->placement.num_placement; i++)
		bo->placements[i] &= ~TTM_PL_FLAG_NO_EVICT;
	r = ttm_bo_validate(&bo->tbo, &bo->placement, false, false, false);
	if (unlikely(r != 0))
		dev_err(bo->qdev->dev, "%p validate failed for unpin\n", bo);
	return r;
}

void qxl_bo_force_delete(struct qxl_device *qdev)
{
	struct qxl_bo *bo, *n;

	if (list_empty(&qdev->gem.objects))
		return;
	dev_err(qdev->dev, "Userspace still has active objects !\n");
	list_for_each_entry_safe(bo, n, &qdev->gem.objects, list) {
		mutex_lock(&qdev->ddev->struct_mutex);
		dev_err(qdev->dev, "%p %p %lu %lu force free\n",
			&bo->gem_base, bo, (unsigned long)bo->gem_base.size,
			*((unsigned long *)&bo->gem_base.refcount));
		mutex_lock(&bo->qdev->gem.mutex);
		list_del_init(&bo->list);
		mutex_unlock(&bo->qdev->gem.mutex);
		/* this should unref the ttm bo */
		drm_gem_object_unreference(&bo->gem_base);
		mutex_unlock(&qdev->ddev->struct_mutex);
	}
}

int qxl_bo_init(struct qxl_device *qdev)
{
	return qxl_ttm_init(qdev);
}

void qxl_bo_fini(struct qxl_device *qdev)
{
	qxl_ttm_fini(qdev);
}
