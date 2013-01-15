/*
 * Copyright 2011 Red Hat, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "qxl_drv.h"
#include "qxl_object.h"

/* returns a pointer to the already allocated qxl_rect array inside
 * the qxl_clip_rects. This is *not* the same as the memory allocated
 * on the device, it is offset to qxl_clip_rects.chunk.data */
static struct qxl_rect *drawable_set_clipping(struct qxl_device *qdev,
					      struct qxl_drawable *drawable,
					      unsigned num_clips,
					      struct drm_qxl_release *release)
{
	struct qxl_clip_rects *dev_clips;

	dev_clips = qxl_allocnf(qdev, sizeof(*dev_clips) +
				sizeof(struct qxl_rect) * num_clips, release);
	dev_clips->num_rects = num_clips;
	dev_clips->chunk.next_chunk = 0;
	dev_clips->chunk.prev_chunk = 0;
	dev_clips->chunk.data_size = sizeof(struct qxl_rect) * num_clips;
	drawable->clip.type = SPICE_CLIP_TYPE_RECTS;
	drawable->clip.data = qxl_bo_physical_address(qdev,
						      release->bos[release->bo_count - 1], 0);
	return (struct qxl_rect *)dev_clips->chunk.data;
}

uint64_t
qxl_release_alloc(struct qxl_device *qdev, int type,
		  struct drm_qxl_release **ret)
{
	struct drm_qxl_release *release;
	int handle = 0;
	size_t size = sizeof(*release);
	int idr_ret;

	release = kzalloc(size, GFP_KERNEL);
	if (!release) {
		DRM_ERROR("Out of memory\n");
		return 0;
	}
	release->type = type;
	release->bo_count = 0;
again:
	if (idr_pre_get(&qdev->release_idr, GFP_KERNEL) == 0) {
		DRM_ERROR("Out of memory for release idr\n");
		kfree(release);
		goto release_fail;
	}
	spin_lock(&qdev->release_idr_lock);
	idr_ret = idr_get_new_above(&qdev->release_idr, release, 1, &handle);
	spin_unlock(&qdev->release_idr_lock);
	if (idr_ret == -EAGAIN)
		goto again;
	if (ret)
		*ret = release;
	QXL_INFO(qdev, "allocated release %lld\n", handle);
	release->id = handle;
release_fail:

	return handle;
}

void
qxl_release_free(struct qxl_device *qdev,
		 struct drm_qxl_release *release)
{
	int i;

	QXL_INFO(qdev, "release %d, type %d, %d bos\n", release->id,
		 release->type, release->bo_count);
	for (i = 0 ; i < release->bo_count; ++i) {
		QXL_INFO(qdev, "release %llx\n",
			release->bos[i]->tbo.addr_space_offset
						- DRM_FILE_OFFSET);
		qxl_fence_remove_release(&release->bos[i]->fence, release->id);
		qxl_bo_unref(&release->bos[i]);
	}
	spin_lock(&qdev->release_idr_lock);
	idr_remove(&qdev->release_idr, release->id);
	spin_unlock(&qdev->release_idr_lock);
	kfree(release);
}

void
qxl_release_add_res(struct qxl_device *qdev, struct drm_qxl_release *release,
		    struct qxl_bo *bo)
{
	if (release->bo_count >= QXL_MAX_RES) {
		DRM_ERROR("exceeded max resource on a drm_qxl_release item\n");
		return;
	}
	release->bos[release->bo_count++] = bo;
}

void *qxl_alloc_releasable(struct qxl_device *qdev, unsigned long size,
			   int type, struct drm_qxl_release **release,
			   struct qxl_bo **bo)
{
	int idr_ret;
	void *ret;

	idr_ret = qxl_release_alloc(qdev, type, release);
	ret = qxl_allocnf(qdev, size, *release);
	((union qxl_release_info *)ret)->id = idr_ret;
	*bo = (*release)->bos[(*release)->bo_count - 1];
	return ret;
}

int qxl_fence_releaseable(struct qxl_device *qdev,
			  struct drm_qxl_release *release)
{
	int i, ret;
	for (i = 0; i < release->bo_count; i++) {
		if (!release->bos[i]->tbo.sync_obj)
			release->bos[i]->tbo.sync_obj = &release->bos[i]->fence;
		ret = qxl_fence_add_release(&release->bos[i]->fence, release->id);
		if (ret)
			return ret;
	}
	return 0;
}

static struct qxl_drawable *
make_drawable(struct qxl_device *qdev, int surface, uint8_t type,
	      const struct qxl_rect *rect,
	      struct drm_qxl_release **release,
	      struct qxl_bo **drawable_bo)
{
	struct qxl_drawable *drawable;
	int i;

	drawable = qxl_alloc_releasable(qdev, sizeof(*drawable),
					QXL_RELEASE_DRAWABLE, release,
					drawable_bo);

	drawable->type = type;

	drawable->surface_id = surface;		/* Only primary for now */
	drawable->effect = QXL_EFFECT_OPAQUE;
	drawable->self_bitmap = 0;
	drawable->self_bitmap_area.top = 0;
	drawable->self_bitmap_area.left = 0;
	drawable->self_bitmap_area.bottom = 0;
	drawable->self_bitmap_area.right = 0;
	/* FIXME: add clipping */
	drawable->clip.type = SPICE_CLIP_TYPE_NONE;

	/*
	 * surfaces_dest[i] should apparently be filled out with the
	 * surfaces that we depend on, and surface_rects should be
	 * filled with the rectangles of those surfaces that we
	 * are going to use.
	 */
	for (i = 0; i < 3; ++i)
		drawable->surfaces_dest[i] = -1;

	if (rect)
		drawable->bbox = *rect;

	drawable->mm_time = qdev->rom->mm_clock;

	return drawable;
}

/* TODO: bo per command is wasteful. add an offset */
void
qxl_push_command_ring(struct qxl_device *qdev, struct qxl_bo *bo, uint32_t type)
{
	struct qxl_command cmd;

	cmd.type = type;
	cmd.data = qxl_bo_physical_address(qdev, bo, 0);

	qxl_ring_push(qdev->command_ring, &cmd);
}

void
qxl_push_cursor_ring(struct qxl_device *qdev, struct qxl_bo *bo, uint32_t type)
{
	struct qxl_command cmd;

	cmd.type = type;
	cmd.data = qxl_bo_physical_address(qdev, bo, 0);

	qxl_ring_push(qdev->cursor_ring, &cmd);
}

static void
push_drawable(struct qxl_device *qdev, struct qxl_bo *drawable_bo)
{
	qxl_push_command_ring(qdev, drawable_bo, QXL_CMD_DRAW);
}

static struct qxl_palette *qxl_palette_create_1bit(
			struct drm_qxl_release *release,
			const struct qxl_fb_image *qxl_fb_image)
{
	struct qxl_device *qdev = qxl_fb_image->qdev;
	const struct fb_image *fb_image = &qxl_fb_image->fb_image;
	uint32_t visual = qxl_fb_image->visual;
	const uint32_t *pseudo_palette = qxl_fb_image->pseudo_palette;
	struct qxl_palette *ret;
	uint32_t fgcolor, bgcolor;
	static uint64_t unique; /* we make no attempt to actually set this
				 * correctly globaly, since that would require
				 * tracking all of our palettes. */

	ret = qxl_allocnf(qdev,
			  sizeof(struct qxl_palette) + sizeof(uint32_t) * 2,
			  release);
	ret->num_ents = 2;
	ret->unique = unique++;
	if (visual == FB_VISUAL_TRUECOLOR || visual == FB_VISUAL_DIRECTCOLOR) {
		/* NB: this is the only used branch currently. */
		fgcolor = pseudo_palette[fb_image->fg_color];
		bgcolor = pseudo_palette[fb_image->bg_color];
	} else {
		fgcolor = fb_image->fg_color;
		bgcolor = fb_image->bg_color;
	}
	ret->ents[0] = bgcolor;
	ret->ents[1] = fgcolor;
	return ret;
}

void qxl_draw_opaque_fb(const struct qxl_fb_image *qxl_fb_image,
			int stride /* filled in if 0 */)
{
	struct qxl_device *qdev = qxl_fb_image->qdev;
	struct qxl_drawable *drawable;
	struct qxl_rect rect;
	struct qxl_image *image;
	struct qxl_palette *palette;
	const struct fb_image *fb_image = &qxl_fb_image->fb_image;
	int x = fb_image->dx;
	int y = fb_image->dy;
	int width = fb_image->width;
	int height = fb_image->height;
	const char *src = fb_image->data;
	int depth = fb_image->depth;
	struct drm_qxl_release *release;
	struct qxl_bo *image_bo;
	struct qxl_bo *drawable_bo;

	if (stride == 0)
		stride = depth * width / 8;

	rect.left = x;
	rect.right = x + width;
	rect.top = y;
	rect.bottom = y + height;

	drawable = make_drawable(qdev, 0, QXL_DRAW_COPY, &rect, &release,
				 &drawable_bo);
	QXL_INFO(qdev, "drawable off %llx, release id %lld\n",
		 drawable_bo->tbo.addr_space_offset - DRM_FILE_OFFSET,
		 drawable->release_info.id);

	drawable->u.copy.src_area.top = 0;
	drawable->u.copy.src_area.bottom = height;
	drawable->u.copy.src_area.left = 0;
	drawable->u.copy.src_area.right = width;

	drawable->u.copy.rop_descriptor = SPICE_ROPD_OP_PUT;
	drawable->u.copy.scale_mode = 0;
	drawable->u.copy.mask.flags = 0;
	drawable->u.copy.mask.pos.x = 0;
	drawable->u.copy.mask.pos.y = 0;
	drawable->u.copy.mask.bitmap = 0;

	image = qxl_image_create(
		qdev, release, &image_bo,
		(const uint8_t *)src, 0, 0, width, height, depth, stride);
	QXL_INFO(qdev, "image_bo offset %llx\n",
		 image_bo->tbo.addr_space_offset - DRM_FILE_OFFSET);
	if (depth == 1) {
		struct qxl_bo *palette_bo;

		palette = qxl_palette_create_1bit(release, qxl_fb_image);
		palette_bo = release->bos[release->bo_count - 1];
		image->u.bitmap.palette =
			qxl_bo_physical_address(qdev, palette_bo, 0);
	}
	drawable->u.copy.src_bitmap =
		qxl_bo_physical_address(qdev, image_bo, 0);

	push_drawable(qdev, drawable_bo);
}

#if 0
static void debug_fill_image(uint8_t *data, long width, long height,
			     long stride)
{
	int x, y;
	static int c = 1;

	DRM_INFO("%s: data %p, w %ld, h %ld, stride %ld\n", __func__, data,
		width, height, stride);
	for (y = height; y > 0; y--) {
		/* DRM_INFO("%s: %d, data %p\n", __func__, y, data); */
		for (x = 2; x < width * 4; x += 4)
			data[x] += c;
		data += stride;
	}
	c += 127;
}
#else
#define debug_fill_image(...)
#endif

/* push a draw command using the given clipping rectangles as
 * the sources from the shadow framebuffer.
 *
 * Right now implementing with a single draw and a clip list. Clip
 * lists are known to be a problem performance wise, this can be solved
 * by treating them differently in the server.
 */
void qxl_draw_dirty_fb(struct qxl_device *qdev,
		       struct qxl_framebuffer *qxl_fb,
		       struct qxl_bo *bo,
		       unsigned flags, unsigned color,
		       struct drm_clip_rect *clips,
		       unsigned num_clips, int inc)
{
	/*
	 * TODO: only a single monitor (stole this code from vmwgfx_kms.c)
	 * vmwgfx command created a blit command for each crt that any of
	 * the clip rects targeted.
	 */
	/*
	 * TODO: if flags & DRM_MODE_FB_DIRTY_ANNOTATE_FILL then we should
	 * send a fill command instead, much cheaper.
	 *
	 * See include/drm/drm_mode.h
	 */
	struct drm_clip_rect *clips_ptr;
	int i;
	int left, right, top, bottom;
	int width, height;
	struct qxl_drawable *drawable;
	struct qxl_rect drawable_rect;
	struct qxl_rect *rects;
	struct qxl_image *image;
	int stride = qxl_fb->base.pitches[0];
	/* depth is not actually interesting, we don't mask with it */
	int depth = qxl_fb->base.bits_per_pixel;
	uint8_t *surface_base = bo->kptr;
	struct drm_qxl_release *release;
	struct qxl_bo *image_bo;
	struct qxl_bo *drawable_bo;

	left = clips->x1;
	right = clips->x2;
	top = clips->y1;
	bottom = clips->y2;

	/* skip the first clip rect */
	for (i = 1, clips_ptr = clips + inc;
	     i < num_clips; i++, clips_ptr += inc) {
		left = min_t(int, left, (int)clips_ptr->x1);
		right = max_t(int, right, (int)clips_ptr->x2);
		top = min_t(int, top, (int)clips_ptr->y1);
		bottom = max_t(int, bottom, (int)clips_ptr->y2);
	}

	width = right - left;
	height = bottom - top;
	drawable_rect.left = left;
	drawable_rect.right = right;
	drawable_rect.top = top;
	drawable_rect.bottom = bottom;
	drawable = make_drawable(qdev, 0, QXL_DRAW_COPY, &drawable_rect,
				 &release, &drawable_bo);
	rects = drawable_set_clipping(qdev, drawable, num_clips, release);

	drawable->u.copy.src_area.top = 0;
	drawable->u.copy.src_area.bottom = height;
	drawable->u.copy.src_area.left = 0;
	drawable->u.copy.src_area.right = width;

	drawable->u.copy.rop_descriptor = SPICE_ROPD_OP_PUT;
	drawable->u.copy.scale_mode = 0;
	drawable->u.copy.mask.flags = 0;
	drawable->u.copy.mask.pos.x = 0;
	drawable->u.copy.mask.pos.y = 0;
	drawable->u.copy.mask.bitmap = 0;

	debug_fill_image((surface_base + (left * 4) + (stride * top)),
			width, height, stride);
	image = qxl_image_create(
		qdev, release, &image_bo, surface_base,
		left, top, width, height, depth, stride);
	drawable->u.copy.src_bitmap = qxl_bo_physical_address(qdev, image_bo, 0);

	clips_ptr = clips;
	for (i = 0; i < num_clips; i++, clips_ptr += inc) {
		rects[i].left   = clips_ptr->x1;
		rects[i].right  = clips_ptr->x2;
		rects[i].top    = clips_ptr->y1;
		rects[i].bottom = clips_ptr->y2;
	}
	push_drawable(qdev, drawable_bo);
}

void qxl_draw_copyarea(struct qxl_device *qdev,
		       u32 width, u32 height,
		       u32 sx, u32 sy,
		       u32 dx, u32 dy)
{
	struct qxl_drawable *drawable;
	struct qxl_rect rect;
	struct drm_qxl_release *release;
	struct qxl_bo *drawable_bo;

	rect.left = dx;
	rect.top = dy;
	rect.right = dx + width;
	rect.bottom = dy + height;
	drawable = make_drawable(qdev, 0, QXL_COPY_BITS, &rect, &release,
				 &drawable_bo);
	drawable->u.copy_bits.src_pos.x = sx;
	drawable->u.copy_bits.src_pos.y = sy;

	push_drawable(qdev, drawable_bo);
}

void qxl_draw_fill(struct qxl_draw_fill *qxl_draw_fill_rec)
{
	struct qxl_device *qdev = qxl_draw_fill_rec->qdev;
	struct qxl_rect rect = qxl_draw_fill_rec->rect;
	uint32_t color = qxl_draw_fill_rec->color;
	uint16_t rop = qxl_draw_fill_rec->rop;
	struct qxl_drawable *drawable;
	struct drm_qxl_release *release;
	struct qxl_bo *drawable_bo;

	drawable = make_drawable(qdev, 0, QXL_DRAW_FILL, &rect, &release,
				 &drawable_bo);

	drawable->u.fill.brush.type = SPICE_BRUSH_TYPE_SOLID;
	drawable->u.fill.brush.u.color = color;
	drawable->u.fill.rop_descriptor = rop;
	drawable->u.fill.mask.flags = 0;
	drawable->u.fill.mask.pos.x = 0;
	drawable->u.fill.mask.pos.y = 0;
	drawable->u.fill.mask.bitmap = 0;

	push_drawable(qdev, drawable_bo);
}
