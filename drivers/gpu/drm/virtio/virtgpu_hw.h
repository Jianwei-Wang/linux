#ifndef VIRTGPU_HW_H
#define VIRTGPU_HW_H

#define VIRTGPU_CMD_HAS_RESP (1 << 31)
#define VIRTGPU_CMD_3D_ONLY  (1 << 30)
enum virtgpu_ctrl_cmd {
	VIRTGPU_CMD_NOP,
	VIRTGPU_CMD_GET_DISPLAY_INFO = (1 | VIRTGPU_CMD_HAS_RESP),
	VIRTGPU_CMD_GET_CAPS = (2 | VIRTGPU_CMD_HAS_RESP),
	VIRTGPU_CMD_RESOURCE_CREATE_2D = 3,
	VIRTGPU_CMD_RESOURCE_UNREF = 4,
	VIRTGPU_CMD_SET_SCANOUT = 5,
	VIRTGPU_CMD_RESOURCE_FLUSH = 6,
	VIRTGPU_CMD_TRANSFER_TO_HOST_2D = 7,
	VIRTGPU_CMD_RESOURCE_ATTACH_BACKING = 8,
	VIRTGPU_CMD_RESOURCE_INVAL_BACKING = 9,
       
	VIRTGPU_CMD_CTX_CREATE = (10 | VIRTGPU_CMD_3D_ONLY),
	VIRTGPU_CMD_CTX_DESTROY = (11 | VIRTGPU_CMD_3D_ONLY),
	VIRTGPU_CMD_CTX_ATTACH_RESOURCE = (12 | VIRTGPU_CMD_3D_ONLY),
	VIRTGPU_CMD_CTX_DETACH_RESOURCE = (13 | VIRTGPU_CMD_3D_ONLY),

	VIRTGPU_CMD_RESOURCE_CREATE_3D = (14 | VIRTGPU_CMD_3D_ONLY),

	VIRTGPU_CMD_TRANSFER_TO_HOST_3D = (15 | VIRTGPU_CMD_3D_ONLY),
	VIRTGPU_CMD_TRANSFER_FROM_HOST_3D = (16 | VIRTGPU_CMD_3D_ONLY),

	VIRTGPU_CMD_SUBMIT_3D = (17 | VIRTGPU_CMD_3D_ONLY),
};

enum virtgpu_ctrl_event {
	VIRTGPU_EVENT_NOP,
	VIRTGPU_EVENT_ERROR,
	VIRTGPU_EVENT_DISPLAY_CHANGE,
};

/* data passed in the cursor vq */
struct virtgpu_hw_cursor_page {
	uint32_t cursor_x, cursor_y;
	uint32_t cursor_hot_x, cursor_hot_y;
	uint32_t cursor_id;
};

struct virtgpu_resource_unref {
	uint32_t resource_id;
};

/* create a simple 2d resource with a format */
struct virtgpu_resource_create_2d {
	uint32_t resource_id;
	uint32_t format;
	uint32_t width;
	uint32_t height;
};

struct virtgpu_set_scanout {
	uint32_t scanout_id;
	uint32_t resource_id;
	uint32_t width;
	uint32_t height;
	uint32_t x;
	uint32_t y;
};

struct virtgpu_resource_flush {
	uint32_t resource_id;
	uint32_t width;
	uint32_t height;
	uint32_t x;
	uint32_t y;
};

/* simple transfer to_host */
struct virtgpu_transfer_to_host_2d {
	uint32_t resource_id;
	uint32_t offset;
	uint32_t width;
	uint32_t height;
	uint32_t x;
	uint32_t y;
};

struct virtgpu_mem_entry {
	uint64_t addr;
	uint32_t length;
	uint32_t pad;
};

struct virtgpu_resource_attach_backing {
	uint32_t resource_id;
	uint32_t nr_entries;
};

struct virtgpu_resource_inval_backing {
	uint32_t resource_id;
};

#define VIRTGPU_MAX_SCANOUTS 16
struct virtgpu_display_info {
	uint32_t num_scanouts;
	struct {
		uint32_t enabled;
		uint32_t width;
		uint32_t height;
		uint32_t x;
		uint32_t y;
		uint32_t flags;
	} pmodes[VIRTGPU_MAX_SCANOUTS];
};


/* 3d related */
struct virtgpu_box {
	uint32_t x, y, z;
	uint32_t w, h, d;
};

struct virtgpu_transfer_to_host_3d {
	uint64_t data;
	uint32_t resource_id;
	uint32_t level;
	struct virtgpu_box box;
	uint32_t stride;
	uint32_t layer_stride;
	uint32_t ctx_id;
};

struct virtgpu_transfer_from_host_3d {
	uint64_t data;
	uint32_t resource_id;
	uint32_t level;
	struct virtgpu_box box;
	uint32_t stride;
	uint32_t layer_stride;
	uint32_t ctx_id;
};

#define VIRTGPU_RESOURCE_FLAG_Y_0_TOP (1 << 0)
struct virtgpu_resource_create_3d {
	uint32_t resource_id;
	uint32_t target;
	uint32_t format;
	uint32_t bind;
	uint32_t width;
	uint32_t height;
	uint32_t depth;
	uint32_t array_size;
	uint32_t last_level;
	uint32_t nr_samples;
	uint32_t flags;
};

struct virtgpu_ctx_create {
	uint32_t ctx_id;
	uint32_t nlen;
	char debug_name[64];
};

struct virtgpu_ctx_destroy {
	uint32_t ctx_id;
};

struct virtgpu_ctx_resource {
	uint32_t resource_id;
	uint32_t ctx_id;
};

struct virtgpu_cmd_submit {
	uint64_t phy_addr;
	uint32_t size;
	uint32_t ctx_id;
};

struct virtgpu_command {
	uint32_t type;
	uint32_t flags;
	uint64_t rsvd;
	union virtgpu_cmds {
		struct virtgpu_resource_create_2d resource_create_2d;
		struct virtgpu_resource_unref resource_unref;
		struct virtgpu_resource_flush resource_flush;
		struct virtgpu_set_scanout set_scanout;
		struct virtgpu_transfer_to_host_2d transfer_to_host_2d;
		struct virtgpu_resource_attach_backing resource_attach_backing;
		struct virtgpu_resource_inval_backing resource_inval_backing;

		struct virtgpu_cmd_submit cmd_submit;
		struct virtgpu_ctx_create ctx_create;
		struct virtgpu_ctx_destroy ctx_destroy;
		struct virtgpu_ctx_resource ctx_resource;
		struct virtgpu_resource_create_3d resource_create_3d;
		struct virtgpu_transfer_to_host_3d transfer_to_host_3d;
		struct virtgpu_transfer_from_host_3d transfer_from_host_3d;
	} u;
};

struct virtgpu_response {
	uint32_t type;
	uint32_t flags;
	union virtgpu_resps {
		struct virtgpu_display_info display_info;
	} u;
};

struct virtgpu_event {
	uint32_t type;
	uint32_t err_code;
	union virtgpu_events {
		struct virtgpu_display_info display_info;
	} u;
};

/* simple formats for fbcon/X use */
enum virtgpu_formats {
   VIRGL_FORMAT_B8G8R8A8_UNORM          = 1,
   VIRGL_FORMAT_B8G8R8X8_UNORM          = 2,
   VIRGL_FORMAT_A8R8G8B8_UNORM          = 3,
   VIRGL_FORMAT_X8R8G8B8_UNORM          = 4,

   VIRGL_FORMAT_B5G5R5A1_UNORM          = 5,

   VIRGL_FORMAT_R8_UNORM                = 64,
};

#endif
