#ifndef QXL_DRV_H
#define QXL_DRV_H

/*
 * Definitions taken from spice-protocol, plus kernel driver specific bits.
 */

#include <linux/workqueue.h>
#include <linux/firmware.h>
#include <linux/platform_device.h>

#include "drmP.h"
#include "drm_crtc.h"
#include <ttm/ttm_bo_api.h>
#include <ttm/ttm_bo_driver.h>
#include <ttm/ttm_placement.h>
#include <ttm/ttm_module.h>

#include <drm/qxl_drm.h>
#include "qxl_dev.h"

#define DRIVER_AUTHOR		"Dave Airlie"

#define DRIVER_NAME		"qxl"
#define DRIVER_DESC		"RH QXL"
#define DRIVER_DATE		"20120117"

#define DRIVER_MAJOR 0
#define DRIVER_MINOR 1
#define DRIVER_PATCHLEVEL 0

#define QXL_NUM_OUTPUTS 1
 
#define QXL_DEBUGFS_MAX_COMPONENTS		32

extern int qxl_log_level;
extern int qxl_debug_disable_fb;

enum {
	QXL_INFO_LEVEL = 1,
	QXL_DEBUG_LEVEL = 2,
};

#define QXL_INFO(qdev, fmt, ...) do { \
		if (qxl_log_level >= QXL_INFO_LEVEL) {	\
			qxl_io_log(qdev, fmt, __VA_ARGS__); \
		}	\
	} while (0)
#define QXL_DEBUG(qdev, fmt, ...) do { \
		if (qxl_log_level >= QXL_DEBUG_LEVEL) {	\
			qxl_io_log(qdev, fmt, __VA_ARGS__); \
		}	\
	} while (0)
#define QXL_INFO_ONCE(qdev, fmt, ...) do { \
		static int done;		\
		if (!done) {			\
			done = 1;			\
			QXL_INFO(qdev, fmt, __VA_ARGS__);	\
		}						\
	} while (0)

#define DRM_FILE_OFFSET 0x100000000ULL
#define DRM_FILE_PAGE_OFFSET (DRM_FILE_OFFSET >> PAGE_SHIFT)

#define QXL_INTERRUPT_MASK (\
	QXL_INTERRUPT_DISPLAY |\
	QXL_INTERRUPT_CURSOR |\
	QXL_INTERRUPT_IO_CMD |\
	QXL_INTERRUPT_CLIENT_MONITORS_CONFIG)
/* TODO QXL_INTERRUPT_CLIENT */

struct qxl_bo {
	/* Protected by gem.mutex */
	struct list_head		list;
	/* Protected by tbo.reserved */
	u32				placements[3];
	struct ttm_placement		placement;
	struct ttm_buffer_object	tbo;
	struct ttm_bo_kmap_obj		kmap;
	unsigned			pin_count;
	void				*kptr;
	u32				pitch;
	int                             type;
	/* Constant after initialization */
	struct qxl_device		*qdev;
	struct drm_gem_object		gem_base;
	struct qxl_surface surf;
	uint32_t surface_id;
};
#define gem_to_qxl_bo(gobj) container_of((gobj), struct qxl_bo, gem_base)


struct qxl_fence_driver {
	atomic_t seq;
	uint32_t last_seq;
	wait_queue_head_t queue;
	rwlock_t lock;
	struct list_head created;
	struct list_head emited;
	struct list_head signaled;
};

struct qxl_fence {
	struct qxl_device *qdev;
	struct kref kref;
	struct list_head list;
	uint32_t seq;
	unsigned long timeout;
	bool emited;
	bool signaled;
};

int qxl_fence_driver_init(struct qxl_device *qdev);
void qxl_fence_driver_fini(struct qxl_device *qdev);

struct qxl_gem {
	struct mutex		mutex;
	struct list_head	objects;
};

struct qxl_crtc {
	struct drm_crtc base;
	int cur_x;
	int cur_y;
//	int hot_x;
//	int hot_y;
};

struct qxl_output {
	int index;
	struct drm_connector base;
	struct drm_encoder enc;
};

struct qxl_framebuffer {
	struct drm_framebuffer base;
	struct drm_gem_object *obj;
};

#define to_qxl_crtc(x) container_of(x, struct qxl_crtc, base)
#define drm_connector_to_qxl_output(x) container_of(x, struct qxl_output, base)
#define drm_encoder_to_qxl_output(x) container_of(x, struct qxl_output, base)
#define to_qxl_framebuffer(x) container_of(x, struct qxl_framebuffer, base)

struct qxl_mman {
	struct ttm_bo_global_ref        bo_global_ref;
	struct drm_global_reference	mem_global_ref;
	bool				mem_global_referenced;
	struct ttm_bo_device		bdev;
};

struct qxl_mode_info {
	int num_modes;
	struct qxl_mode *modes;
	bool mode_config_initialized;

	/* pointer to fbdev info structure */
	struct qxl_fbdev *qfbdev;
};


struct qxl_memslot {
	uint8_t		generation;
	uint64_t	start_phys_addr;
	uint64_t	end_phys_addr;
	uint64_t	high_bits;
};

enum {
	QXL_RELEASE_DRAWABLE,
	QXL_RELEASE_SURFACE_CMD,
	QXL_RELEASE_CURSOR_CMD,
};

/* drm_ prefix to differentiate from qxl_release_info in
 * spice-protocol/qxl_dev.h */
#define QXL_MAX_RES 6
struct drm_qxl_release {
	int id;
	int type;
	int bo_count;
	struct qxl_bo *bos[QXL_MAX_RES];
};

/* all information required for an image blit. used instead
 * of fb_image & fb_info to do a single allocation when we need
 * to queue the work. */
struct qxl_fb_image {
	struct qxl_device *qdev;
	uint32_t pseudo_palette[16];
	struct fb_image fb_image;
	uint32_t visual;
};

struct qxl_draw_fill {
	struct qxl_device *qdev;
	struct qxl_rect rect;
	uint32_t color;
	uint16_t rop;
};

enum {
	QXL_FB_WORK_ITEM_INVALID,
	QXL_FB_WORK_ITEM_IMAGE,
	QXL_FB_WORK_ITEM_DRAW_FILL,
};

struct qxl_fb_work_item {
	struct list_head head;
	int type;
	union {
		struct qxl_fb_image qxl_fb_image;
		struct qxl_draw_fill qxl_draw_fill;
	};
};

/*
 * Debugfs
 */
struct qxl_debugfs {
	struct drm_info_list	*files;
	unsigned		num_files;
};

int qxl_debugfs_add_files(struct qxl_device *rdev,
			     struct drm_info_list *files,
			     unsigned nfiles);
int qxl_debugfs_fence_init(struct qxl_device *rdev);

struct qxl_device;

struct qxl_device {
	struct device			*dev;
	struct drm_device		*ddev;
	struct pci_dev			*pdev;
	unsigned long flags;

	resource_size_t vram_base, vram_size;
	resource_size_t surfaceram_base, surfaceram_size;
	resource_size_t rom_base, rom_size;
	struct qxl_rom *rom;

	struct qxl_mode *modes;
	struct qxl_bo *monitors_config_bo;
	struct qxl_monitors_config *monitors_config;

	/* last received client_monitors_config */
	struct qxl_monitors_config *client_monitors_config;

	int io_base;
	void *ram;
	struct qxl_bo *surface0_bo;
	void  *surface0_shadow;
	struct qxl_mman		mman;
	struct qxl_gem		gem;
	struct qxl_mode_info mode_info;

	/*
	 * last created framebuffer with fb_create
	 * only used by debugfs dumbppm
	 */
	struct qxl_framebuffer *active_user_framebuffer;

	struct fb_info			*fbdev_info;
	struct qxl_framebuffer	*fbdev_qfb;
	void *ram_physical;

	struct qxl_ring *release_ring;
	struct qxl_ring *command_ring;
	struct qxl_ring *cursor_ring;

	struct qxl_ram_header *ram_header;
	bool mode_set;

	bool primary_created;
	unsigned primary_width;
	unsigned primary_height;

	struct qxl_memslot	*mem_slots;
	uint8_t		n_mem_slots;

	uint8_t		main_mem_slot;
	uint8_t		surfaces_mem_slot;
	uint8_t		slot_id_bits;
	uint8_t		slot_gen_bits;
	uint64_t	va_slot_mask;

	struct idr	release_idr;
	spinlock_t release_idr_lock;
	struct mutex	async_io_mutex;

	/* framebuffer. workqueue to avoid bo allocation in interrupt context */
	struct workqueue_struct *fb_workqueue;
	spinlock_t		 fb_workqueue_spinlock;
	struct work_struct	 fb_work;
	struct list_head	 fb_work_item_pending;
	struct list_head	 fb_work_item_free;
	struct qxl_fb_work_item  fb_work_items[16];

	/* interrupt handling */
	atomic_t irq_received;
	atomic_t irq_received_display;
	atomic_t irq_received_cursor;
	atomic_t irq_received_io_cmd;
	unsigned irq_received_error;
	wait_queue_head_t display_event;
	wait_queue_head_t cursor_event;
	wait_queue_head_t io_cmd_event;
	struct work_struct client_monitors_config_work;

	/* debugfs */
	struct qxl_debugfs	debugfs[QXL_DEBUGFS_MAX_COMPONENTS];
	unsigned 		debugfs_count;

	struct mutex		update_area_mutex;

	struct idr	surf_id_idr;
	spinlock_t surf_id_idr_lock;	
};

static inline unsigned
qxl_surface_width(struct qxl_device *qdev, unsigned surface_id) {
	return surface_id == 0 ? qdev->primary_width : 0;
}

static inline unsigned
qxl_surface_height(struct qxl_device *qdev, unsigned surface_id) {
	return surface_id == 0 ? qdev->primary_height : 0;
}

/* forward declaration for QXL_INFO_IO */
void qxl_io_log(struct qxl_device *qdev, const char *fmt, ...);

extern struct drm_ioctl_desc qxl_ioctls[];
extern int qxl_max_ioctl;

int qxl_driver_load(struct drm_device *dev, unsigned long flags);
int qxl_driver_unload(struct drm_device *dev);

int qxl_modeset_init(struct qxl_device *qdev);
void qxl_modeset_fini(struct qxl_device *qdev);

int qxl_bo_init(struct qxl_device *qdev);
void qxl_bo_fini(struct qxl_device *qdev);

struct qxl_ring *qxl_ring_create(struct qxl_ring_header *header,
				 int element_size,
				 int n_elements,
				 int prod_notify,
				 wait_queue_head_t *push_event);
void qxl_ring_free(struct qxl_ring *ring);
extern void *qxl_allocnf(struct qxl_device *qdev, unsigned long size,
			 struct drm_qxl_release *release);


static inline void *
qxl_fb_virtual_address(struct qxl_device *qdev, unsigned long physical)
{
	QXL_INFO(qdev, "not implemented (%lu)\n", physical);
	return 0;
}

static inline uint64_t
qxl_bo_physical_address(struct qxl_device *qdev, struct qxl_bo *bo,
			unsigned long offset)
{
	int slot_id = bo->type == QXL_GEM_DOMAIN_VRAM ? qdev->main_mem_slot : qdev->surfaces_mem_slot;
	struct qxl_memslot *slot = &(qdev->mem_slots[slot_id]);

	/* TODO - need to hold one of the locks to read tbo.offset */
	return slot->high_bits | (bo->tbo.offset + offset);
}

/* qxl_fb.c */
#define QXLFB_CONN_LIMIT 1

int qxl_fbdev_init(struct qxl_device *qdev);
void qxl_fbdev_fini(struct qxl_device *qdev);
int qxl_get_handle_for_primary_fb(struct qxl_device *qdev,
				  struct drm_file *file_priv,
				  uint32_t *handle);

/* qxl_display.c */
void
qxl_framebuffer_init(struct drm_device *dev,
		     struct qxl_framebuffer *rfb,
		     struct drm_mode_fb_cmd2 *mode_cmd,
		     struct drm_gem_object *obj);
void qxl_display_read_client_monitors_config(struct qxl_device *qdev);
void qxl_send_monitors_config(struct qxl_device *qdev);

/* used by qxl_debugfs only */
void qxl_crtc_set_from_monitors_config(struct qxl_device *qdev);
void qxl_alloc_client_monitors_config(struct qxl_device *qdev, unsigned count);

/* qxl_gem.c */
int qxl_gem_init(struct qxl_device *qdev);
void qxl_gem_fini(struct qxl_device *qdev);
int qxl_gem_object_create(struct qxl_device *qdev, int size,
				int alignment, int initial_domain,
				bool discardable, bool kernel,
				struct drm_gem_object **obj);
int qxl_gem_object_pin(struct drm_gem_object *obj, uint32_t pin_domain,
			  uint64_t *gpu_addr);
void qxl_gem_object_unpin(struct drm_gem_object *obj);
int qxl_gem_object_create_with_handle(struct qxl_device *qdev,
				      struct drm_file *file_priv,
				      u32 domain,
				      size_t size,
				      struct qxl_bo **qobj,
				      uint32_t *handle);
int qxl_gem_object_init(struct drm_gem_object *obj);
void qxl_gem_object_free(struct drm_gem_object *gobj);
int qxl_gem_object_open(struct drm_gem_object *obj, struct drm_file *file_priv);
void qxl_gem_object_close(struct drm_gem_object *obj,
			  struct drm_file *file_priv);
void qxl_bo_force_delete(struct qxl_device *qdev);
int qxl_bo_kmap(struct qxl_bo *bo, void **ptr);

/* qxl_dumb.c */
int qxl_mode_dumb_create(struct drm_file *file_priv,
			 struct drm_device *dev,
			 struct drm_mode_create_dumb *args);
int qxl_mode_dumb_destroy(struct drm_file *file_priv,
			  struct drm_device *dev,
			  uint32_t handle);
int qxl_mode_dumb_mmap(struct drm_file *filp,
		       struct drm_device *dev,
		       uint32_t handle, uint64_t *offset_p);


/* qxl ttm */
int qxl_ttm_init(struct qxl_device *qdev);
void qxl_ttm_fini(struct qxl_device *qdev);
int qxl_mmap(struct file *filp, struct vm_area_struct *vma);

/* qxl image */

struct qxl_image *qxl_image_create(struct qxl_device *qdev,
				   struct drm_qxl_release *release,
				   struct qxl_bo **image_bo,
				   const uint8_t *data,
				   int x, int y, int width, int height,
				   int depth, int stride);
void qxl_image_destroy(struct qxl_device *qdev,
		       struct qxl_image *image);
void qxl_update_screen(struct qxl_device *qxl);

/* qxl io operations (qxl_cmd.c) */

void qxl_io_create_primary(struct qxl_device *qdev,
			   unsigned width, unsigned height);
void qxl_io_destroy_primary(struct qxl_device *qdev);
void qxl_io_memslot_add(struct qxl_device *qdev, uint8_t id);
void qxl_io_notify_oom(struct qxl_device *qdev);

int qxl_io_update_area(struct qxl_device *qdev, struct qxl_bo *surf,
		       const struct qxl_rect *area);

void qxl_io_reset(struct qxl_device *qdev);
void qxl_io_monitors_config(struct qxl_device *qdev);
void qxl_ring_push(struct qxl_ring *ring, const void *new_elt);


/*
 * qxl_bo_add_resource.
 *
 */
void qxl_bo_add_resource(struct qxl_bo *main_bo, struct qxl_bo *resource);

/* used both directly via qxl_draw and via ioctl execbuffer */
void *qxl_alloc_releasable(struct qxl_device *qdev, unsigned long size,
			   int type, struct drm_qxl_release **release,
			   struct qxl_bo **bo);
void
qxl_push_command_ring(struct qxl_device *qdev, struct qxl_bo *bo,
		      uint32_t type);
void
qxl_push_cursor_ring(struct qxl_device *qdev, struct qxl_bo *bo,
		     uint32_t type);

/* qxl drawing commands */

void qxl_draw_opaque_fb(const struct qxl_fb_image *qxl_fb_image,
			int stride /* filled in if 0 */);

void qxl_draw_dirty_fb(struct qxl_device *qdev,
		       struct qxl_framebuffer *qxl_fb,
		       struct qxl_bo *bo,
		       unsigned flags, unsigned color,
		       struct drm_clip_rect *clips,
		       unsigned num_clips, int inc);

void qxl_draw_fill(struct qxl_draw_fill *qxl_draw_fill_rec);

void qxl_draw_copyarea(struct qxl_device *qdev,
		       u32 width, u32 height,
		       u32 sx, u32 sy,
		       u32 dx, u32 dy);

uint64_t
qxl_release_alloc(struct qxl_device *qdev, int type,
		  struct drm_qxl_release **ret);

void qxl_release_free(struct qxl_device *qdev,
		      struct drm_qxl_release *release);
void qxl_release_add_res(struct qxl_device *qdev,
			 struct drm_qxl_release *release,
			 struct qxl_bo *bo);
/* used by qxl_debugfs_release */
struct drm_qxl_release *qxl_release_from_id_locked(struct qxl_device *qdev,
						   uint64_t id);

int qxl_garbage_collect(struct qxl_device *qdev);

/* debugfs */

int qxl_debugfs_init(struct drm_minor *minor);
void qxl_debugfs_takedown(struct drm_minor *minor);

/* qxl_irq.c */
int qxl_irq_init(struct qxl_device *qdev);
irqreturn_t qxl_irq_handler(DRM_IRQ_ARGS);

/* qxl_fb.c */
int qxl_fb_init(struct qxl_device *qdev);

/* not static for debugfs */
int qxl_fb_queue_imageblit(struct qxl_device *qdev,
			   struct qxl_fb_image *qxl_fb_image,
			   struct fb_info *info,
			   const struct fb_image *image);
int qxl_fb_queue_draw_fill(struct qxl_draw_fill *qxl_draw_fill_rec);

int qxl_debugfs_add_files(struct qxl_device *qdev,
			  struct drm_info_list *files,
			  unsigned nfiles);

int qxl_surface_id_alloc(struct qxl_device *qdev,
			 struct qxl_bo *surf);
void qxl_surface_id_dealloc(struct qxl_device *qdev,
			    struct qxl_bo *surf);
int qxl_hw_surface_alloc(struct qxl_device *qdev,
			 struct qxl_bo *surf);
int qxl_hw_surface_dealloc(struct qxl_device *qdev,
			   struct qxl_bo *surf);
struct qxl_drv_surface *
qxl_surface_lookup(struct drm_device *dev, int surface_id);
#endif
