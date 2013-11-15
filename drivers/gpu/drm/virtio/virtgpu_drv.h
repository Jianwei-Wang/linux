/*
 * Copyright (C) 2012 Red Hat
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License v2. See the file COPYING in the main directory of this archive for
 * more details.
 */

#ifndef VIRTIO_DRV_H
#define VIRTIO_DRV_H

#include <drm/drmP.h>
#include <drm/drm_crtc_helper.h>
#include <ttm/ttm_bo_api.h>
#include <ttm/ttm_bo_driver.h>
#include <ttm/ttm_placement.h>
#include <ttm/ttm_module.h>

#include "virtgpu_hw.h"
#define DRIVER_NAME "virtio-gpu"
#define DRIVER_DESC "virtio GPU"
#define DRIVER_DATE ""

#define DRIVER_MAJOR 0
#define DRIVER_MINOR 0
#define DRIVER_PATCHLEVEL 1

void virtio_set_driver_bus(struct drm_driver *driver);

struct virtgpu_object {
	struct drm_gem_object gem_base;
	uint32_t hw_res_handle;

	struct sg_table *pages;
	void *vmap;
	bool dumb;
};
#define gem_to_virtgpu_obj(gobj) container_of((gobj), struct virtgpu_object, gem_base)

struct virtgpu_vbuffer;
struct virtgpu_device;

typedef void (*virtgpu_resp_cb)(struct virtgpu_device *vgdev,
				struct virtgpu_vbuffer *vbuf);

struct virtgpu_vbuffer {
	char *buf;
	int size;

	int resp_size;
	char *resp_buf;

	virtgpu_resp_cb resp_cb;

	void *vaddr;
	dma_addr_t busaddr;
	uint32_t vaddr_len;
	struct list_head destroy_list;
};

struct virtgpu_crtc {
	struct drm_crtc base;
	int idx;
	int cur_x;
	int cur_y;
};

struct virtgpu_output {
	int index;
	struct drm_connector base;
	struct drm_encoder enc;
};

struct virtgpu_framebuffer {
	struct drm_framebuffer base;
	struct drm_gem_object *obj;
	int x1, y1, x2, y2; /* dirty rect */
	spinlock_t dirty_lock;
	uint32_t hw_res_handle;
};
#define to_virtgpu_crtc(x) container_of(x, struct virtgpu_crtc, base)
#define drm_connector_to_virtgpu_output(x) container_of(x, struct virtgpu_output, base)
#define drm_encoder_to_virtgpu_output(x) container_of(x, struct virtgpu_output, base)
#define to_virtgpu_framebuffer(x) container_of(x, struct virtgpu_framebuffer, base)

struct virtgpu_mman {
	struct list_head bound_list;
	struct list_head unbound_list;
};

struct virtgpu_fbdev;

struct virtgpu_queue {
	struct virtqueue *vq;
	spinlock_t qlock;
      	wait_queue_head_t ack_queue;
	struct work_struct dequeue_work;
};

struct virtgpu_device {
	struct device *dev;
	struct drm_device *ddev;

	struct virtio_device *vdev;

	struct virtgpu_mman mman;

	/* pointer to fbdev info structure */
	struct virtgpu_fbdev *vgfbdev;

	struct virtgpu_queue eventq;
	struct virtgpu_queue ctrlq;
	struct virtgpu_queue cursorq;

	void *cursor_page;
	struct idr	resource_idr;
	spinlock_t resource_idr_lock;

	int num_outputs;

	wait_queue_head_t resp_wq;
	/* current display info */
	spinlock_t display_info_lock;
	struct virtgpu_display_info display_info;

	int num_hw_scanouts;
};

int virtgpu_driver_load(struct drm_device *dev, unsigned long flags);
int virtgpu_driver_unload(struct drm_device *dev);

/* virtio_gem.c */
int virtgpu_gem_init_object(struct drm_gem_object *obj);
void virtgpu_gem_free_object(struct drm_gem_object *gem_obj);
int virtgpu_gem_init(struct virtgpu_device *qdev);
void virtgpu_gem_fini(struct virtgpu_device *qdev);
struct virtgpu_object *virtgpu_alloc_object(struct drm_device *dev,
					    size_t size);
int virtgpu_gem_object_get_pages(struct virtgpu_object *obj);
int virtgpu_mode_dumb_create(struct drm_file *file_priv,
			     struct drm_device *dev,
			     struct drm_mode_create_dumb *args);
int virtgpu_mode_dumb_destroy(struct drm_file *file_priv,
			      struct drm_device *dev,
			      uint32_t handle);
int virtgpu_mode_dumb_mmap(struct drm_file *file_priv,
			   struct drm_device *dev,
			   uint32_t handle, uint64_t *offset_p);
int virtgpu_gem_fault(struct vm_area_struct *vma, struct vm_fault *vmf);
int virtgpu_drm_gem_mmap(struct file *filp, struct vm_area_struct *vma);
/* virtio_fb */
#define VIRTGPUFB_CONN_LIMIT 1
int virtgpu_fbdev_init(struct virtgpu_device *vgdev);
void virtgpu_fbdev_fini(struct virtgpu_device *vgdev);
int virtgpu_surface_dirty(struct virtgpu_framebuffer *qfb,
			  struct drm_clip_rect *clips,
			  unsigned num_clips);
/* virtio vg */
int virtgpu_resource_id_get(struct virtgpu_device *vgdev, uint32_t *resid);
int virtgpu_cmd_create_resource(struct virtgpu_device *vgdev,
				uint32_t resource_id,
				uint32_t format,
				uint32_t width,
				uint32_t height);
int virtgpu_cmd_transfer_send_2d(struct virtgpu_device *vgdev,
				 uint32_t resource_id, uint32_t offset,
				 uint32_t width, uint32_t height,
				 uint32_t x, uint32_t y);
int virtgpu_cmd_resource_flush(struct virtgpu_device *vgdev,
			       uint32_t resource_id,
			       uint32_t width, uint32_t height,
			       uint32_t x, uint32_t y);
int virtgpu_cmd_set_scanout(struct virtgpu_device *vgdev,
			    uint32_t scanout_id, uint32_t resource_id,
			    uint32_t width, uint32_t height,
			    uint32_t x, uint32_t y);
void virtgpu_ctrl_ack(struct virtqueue *vq);
void virtgpu_cursor_ack(struct virtqueue *vq);
void virtgpu_event_ack(struct virtqueue *vq);
void virtgpu_dequeue_ctrl_func(struct work_struct *work);
void virtgpu_dequeue_cursor_func(struct work_struct *work);
void virtgpu_dequeue_event_func(struct work_struct *work);
int virtgpu_object_attach(struct virtgpu_device *vgdev, struct virtgpu_object *obj, uint32_t resource_id);
int virtgpu_attach_status_page(struct virtgpu_device *vgdev);
int virtgpu_detach_status_page(struct virtgpu_device *vgdev);
void virtgpu_cursor_ping(struct virtgpu_device *vgdev);
int virtgpu_cmd_get_display_info(struct virtgpu_device *vgdev);
int virtgpu_fill_event_vq(struct virtgpu_device *vgdev, int entries);

/* virtgpu_display.c */
int virtgpu_framebuffer_init(struct drm_device *dev,
			     struct virtgpu_framebuffer *vgfb,
			     struct drm_mode_fb_cmd2 *mode_cmd,
			     struct drm_gem_object *obj);
int virtgpu_modeset_init(struct virtgpu_device *vgdev);
void virtgpu_modeset_fini(struct virtgpu_device *vgdev);
#endif
