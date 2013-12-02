
#include <drm/drmP.h>
#include "virtgpu_drv.h"

int virtgpu_gem_init_object(struct drm_gem_object *obj)
{
	/* we do nothings here */
	return 0;
}

void virtgpu_gem_free_object(struct drm_gem_object *gem_obj)
{
	struct virtgpu_object *obj = gem_to_virtgpu_obj(gem_obj);

	if (obj)
		virtgpu_object_unref(&obj);
}

struct virtgpu_object *virtgpu_alloc_object(struct drm_device *dev,
					    size_t size, bool kernel, bool pinned)
{
	struct virtgpu_device *vgdev = dev->dev_private;
	struct virtgpu_object *obj;
	int ret;

	ret = virtgpu_object_create(vgdev, size, kernel, pinned, &obj);
	if (ret)
		return ERR_PTR(ret);

	return obj;
}

int
virtgpu_gem_create(struct drm_file *file,
		   struct drm_device *dev,
		   uint64_t size,
		   struct drm_gem_object **obj_p,
		   uint32_t *handle_p)
{
	struct virtgpu_object *obj;
	int ret;
	u32 handle;

	obj = virtgpu_alloc_object(dev, size, false, false);
	if (obj == NULL)
		return -ENOMEM;

	ret = drm_gem_handle_create(file, &obj->gem_base, &handle);
	if (ret) {
		drm_gem_object_release(&obj->gem_base);
		return ret;
	}

	*obj_p = &obj->gem_base;

	*handle_p = handle;
	return 0;
}

int virtgpu_mode_dumb_create(struct drm_file *file_priv,
			     struct drm_device *dev,
			     struct drm_mode_create_dumb *args)
{
	struct virtgpu_device *vgdev = dev->dev_private;
	struct drm_gem_object *gobj;
	struct virtgpu_object *obj;
	int ret;
	uint32_t pitch;
	uint32_t resid;

	pitch = args->width * ((args->bpp + 1) / 8);
	args->size = pitch * args->height;
	args->size = ALIGN(args->size, PAGE_SIZE);

	ret = virtgpu_gem_create(file_priv, dev, args->size, &gobj,
				 &args->handle);
	if (ret)
		goto fail;

	ret = virtgpu_resource_id_get(vgdev, &resid);
	if (ret)
		goto fail;

	ret = virtgpu_cmd_create_resource(vgdev, resid,
					  2, args->width, args->height);
	if (ret)
		goto fail;

	/* attach the object to the resource */
	obj = gem_to_virtgpu_obj(gobj);
	ret = virtgpu_object_attach(vgdev, obj, resid);
	if (ret)
		goto fail;

	obj->dumb = true;
	args->pitch = pitch;
	drm_gem_object_unreference(&obj->gem_base);
	return ret;
fail:
	return ret;
}

int virtgpu_mode_dumb_destroy(struct drm_file *file_priv,
			      struct drm_device *dev,
			      uint32_t handle)
{
	return drm_gem_handle_delete(file_priv, handle);
}

int virtgpu_mode_dumb_mmap(struct drm_file *file_priv,
			   struct drm_device *dev,
			   uint32_t handle, uint64_t *offset_p)
{
	struct drm_gem_object *gobj;
	struct virtgpu_object *obj;
	BUG_ON(!offset_p);
	gobj = drm_gem_object_lookup(dev, file_priv, handle);
	if (gobj == NULL)
		return -ENOENT;
	obj = gem_to_virtgpu_obj(gobj);
	*offset_p = virtgpu_object_mmap_offset(obj);
	drm_gem_object_unreference_unlocked(gobj);
	return 0;
}

