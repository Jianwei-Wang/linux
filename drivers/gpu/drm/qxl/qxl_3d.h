#ifndef QXL_3D_H
#define QXL_3D_H

#define QXL_3D_COMMAND_RING_SIZE 64

enum qxl_3d_cmd_type {
	QXL_3D_CMD_NOP,
	QXL_3D_CMD_CREATE_CONTEXT,
	QXL_3D_CMD_CREATE_RESOURCE,
	QXL_3D_CMD_SUBMIT,
	QXL_3D_DESTROY_CONTEXT,
	QXL_3D_TRANSFER_GET,
	QXL_3D_TRANSFER_PUT,
	QXL_3D_SET_SCANOUT,
	QXL_3D_FLUSH_BUFFER,
	QXL_3D_RESOURCE_UNREF,
};

/* put a box of data from a BO into a tex/buffer resource */
struct qxl_3d_transfer_put {
	QXLPHYSICAL data;
	uint32_t res_handle;
	struct drm_qxl_3d_box dst_box; /* dst box */
	uint32_t dst_level;
	uint32_t src_stride;
};

struct qxl_3d_transfer_get {
	QXLPHYSICAL data;
	uint32_t res_handle;
	struct drm_qxl_3d_box box;
	int level;
	uint32_t dx, dy;
};

struct qxl_3d_flush_buffer {
	uint32_t res_handle;
	struct drm_qxl_3d_box box;
};

struct qxl_3d_set_scanout {
	uint32_t res_handle;
	struct drm_qxl_3d_box box;
};

struct qxl_3d_resource_create {
	uint32_t handle;
	uint32_t target;
	uint32_t format;
	uint32_t bind;
	uint32_t width;
	uint32_t height;
	uint32_t depth;
	uint32_t array_size;
	uint32_t last_level;
	uint32_t nr_samples;
	uint32_t pad;
};

struct qxl_3d_resource_unref {
	uint32_t res_handle;
};

struct qxl_3d_ring_header {
	uint32_t num_items;
	uint32_t prod;
	uint32_t notify_on_prod;
	uint32_t cons;
	uint32_t notify_on_cons;
};

struct qxl_3d_cmd_submit {
	uint64_t phy_addr;
	uint32_t size;
};

#define QXL_3D_COMMAND_EMIT_FENCE (1 << 0)
struct qxl_3d_command {
	uint32_t type;
	uint32_t flags;
	uint64_t fence_id;
	union qxl_3d_cmds {
		struct qxl_3d_resource_create res_create;
		struct qxl_3d_transfer_put transfer_put;
		struct qxl_3d_transfer_get transfer_get;
		struct qxl_3d_cmd_submit cmd_submit;
		unsigned char pads[112];
		struct qxl_3d_set_scanout set_scanout;
		struct qxl_3d_flush_buffer flush_buffer;
		struct qxl_3d_resource_unref res_unref;
	} u;
};

struct qxl_3d_ram {
	uint32_t version;
	uint32_t pad;
	uint64_t last_fence;
	struct qxl_ring_header  cmd_ring_hdr;
	struct qxl_3d_command	cmd_ring[QXL_3D_COMMAND_RING_SIZE];
};

struct qxl_3d_fence_driver {
	atomic64_t last_seq;
	uint64_t last_activity;
	bool initialized;
	uint64_t			sync_seq;
};

struct qxl_3d_info {
	struct qxl_ring *iv3d_ring;
	void *iv3d;
	struct qxl_3d_ram *ram_3d_header;  
  
	struct qxl_bo *ringbo;

	struct qxl_3d_fence_driver fence_drv;
	wait_queue_head_t		fence_queue;

	struct idr	resource_idr;
	spinlock_t resource_idr_lock;
};


struct qxl_3d_fence {
	uint32_t type;
	struct qxl_device *qdev;
	struct kref kref;
	uint64_t seq;
};

struct qxl_3d_fence *qxl_3d_fence_ref(struct qxl_3d_fence *fence);
void qxl_3d_fence_unref(struct qxl_3d_fence **fence);
 
bool qxl_3d_fence_signaled(struct qxl_3d_fence *fence);
int qxl_3d_fence_wait(struct qxl_3d_fence *fence, bool interruptible);
void qxl_3d_fence_process(struct qxl_device *qdev);

#define QXL_3D_FENCE_SIGNALED_SEQ 0LL
#define QXL_3D_FENCE_JIFFIES_TIMEOUT		(HZ / 2)


#endif
