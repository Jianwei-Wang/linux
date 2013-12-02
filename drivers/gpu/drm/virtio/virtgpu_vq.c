#include <drm/drmP.h>
#include "virtgpu_drv.h"
#include <linux/virtio.h>
#include <linux/virtio_config.h>
#include <linux/virtio_ring.h>


int virtgpu_resource_id_get(struct virtgpu_device *vgdev, uint32_t *resid)
{
	int handle;

	idr_preload(GFP_KERNEL);
	spin_lock(&vgdev->resource_idr_lock);
	handle = idr_alloc(&vgdev->resource_idr, NULL, 1, 0, GFP_NOWAIT);
	spin_unlock(&vgdev->resource_idr_lock);
	idr_preload_end();
	*resid = handle;
	return 0;
}

void virtgpu_resource_id_put(struct virtgpu_device *vgdev, uint32_t id)
{
	spin_lock(&vgdev->resource_idr_lock);
	idr_remove(&vgdev->resource_idr, id);
	spin_unlock(&vgdev->resource_idr_lock);	
}

void virtgpu_ctrl_ack(struct virtqueue *vq)
{
	struct drm_device *dev = vq->vdev->priv;
	struct virtgpu_device *vgdev = dev->dev_private;
	schedule_work(&vgdev->ctrlq.dequeue_work);
}

void virtgpu_cursor_ack(struct virtqueue *vq)
{
	struct drm_device *dev = vq->vdev->priv;
	struct virtgpu_device *vgdev = dev->dev_private;
	schedule_work(&vgdev->cursorq.dequeue_work);
}

void virtgpu_event_ack(struct virtqueue *vq)
{
	struct drm_device *dev = vq->vdev->priv;
	struct virtgpu_device *vgdev = dev->dev_private;
	schedule_work(&vgdev->eventq.dequeue_work);
}

static struct virtgpu_vbuffer *virtgpu_allocate_vbuf(struct virtgpu_device *vgdev,
						     int size, int resp_size,
						     virtgpu_resp_cb resp_cb)
{
	struct virtgpu_vbuffer *vbuf;

	vbuf = kmalloc(sizeof(*vbuf) + size + resp_size, GFP_KERNEL);
	if (!vbuf)
		goto fail;

	vbuf->buf = (void *)vbuf + sizeof(*vbuf);
	vbuf->size = size;
	vbuf->vaddr = NULL;

	vbuf->resp_cb = resp_cb;
	if (resp_size) {
		vbuf->resp_buf = (void *)vbuf->buf + size;
	} else
		vbuf->resp_buf = NULL;
	vbuf->resp_size = resp_size;

	return vbuf;
fail:
	kfree(vbuf);
	return ERR_PTR(-ENOMEM);
}

struct virtgpu_command *virtgpu_alloc_cmd(struct virtgpu_device *vgdev,
					  struct virtgpu_vbuffer **vbuffer_p)
{
	struct virtgpu_vbuffer *vbuf;

	vbuf = virtgpu_allocate_vbuf(vgdev, sizeof(struct virtgpu_command), 0, NULL);
	if (IS_ERR(vbuf)) {
		*vbuffer_p = NULL;
		return ERR_CAST(vbuf);
	}
	*vbuffer_p = vbuf;
	return (struct virtgpu_command *)vbuf->buf;
}

struct virtgpu_command *virtgpu_alloc_cmd_resp(struct virtgpu_device *vgdev,
					       virtgpu_resp_cb cb,
					       struct virtgpu_vbuffer **vbuffer_p)
{
	struct virtgpu_vbuffer *vbuf;

	vbuf = virtgpu_allocate_vbuf(vgdev, sizeof(struct virtgpu_command), sizeof(struct virtgpu_response), cb);
	if (IS_ERR(vbuf)) {
		*vbuffer_p = NULL;
		return ERR_CAST(vbuf);
	}
	*vbuffer_p = vbuf;
	return (struct virtgpu_command *)vbuf->buf;
}

static int add_inbuf(struct virtqueue *vq, struct virtgpu_vbuffer *vbuf)
{
	struct scatterlist sg[1];
	int ret;

	sg_init_one(sg, vbuf->resp_buf, vbuf->resp_size);

	ret = virtqueue_add_inbuf(vq, sg, 1, vbuf, GFP_ATOMIC);
	virtqueue_kick(vq);
	if (!ret)
		ret = vq->num_free;
	return ret;
}

static void free_vbuf(struct virtgpu_device *vgdev, struct virtgpu_vbuffer *vbuf)
{
	if (vbuf->vaddr)
		dma_free_coherent(vgdev->dev, vbuf->vaddr_len, vbuf->vaddr,
				  vbuf->busaddr);

	kfree(vbuf);
}

static int reclaim_vbufs(struct virtqueue *vq, struct list_head *reclaim_list)
{
	struct virtgpu_vbuffer *vbuf;
	unsigned int len;
	int freed = 0;
	while ((vbuf = virtqueue_get_buf(vq, &len))) {
		list_add(&vbuf->destroy_list, reclaim_list);
		freed++;
	}
	return freed;
}
	
void virtgpu_dequeue_ctrl_func(struct work_struct *work)
{
	struct virtgpu_device *vgdev = container_of(work, struct virtgpu_device,
						    ctrlq.dequeue_work);
	int ret;
	struct list_head reclaim_list;
	struct virtgpu_vbuffer *entry, *tmp;
	
	INIT_LIST_HEAD(&reclaim_list);
	spin_lock(&vgdev->ctrlq.qlock);
	do {
		virtqueue_disable_cb(vgdev->ctrlq.vq);
		ret = reclaim_vbufs(vgdev->ctrlq.vq, &reclaim_list);
		if (ret == 0)
			printk("cleaned 0 buffers wierd\n");

	} while (!virtqueue_enable_cb(vgdev->ctrlq.vq));
	spin_unlock(&vgdev->ctrlq.qlock);

	list_for_each_entry_safe(entry, tmp, &reclaim_list, destroy_list) {
		if (entry->resp_cb)
			entry->resp_cb(vgdev, entry);

		list_del(&entry->destroy_list);
		free_vbuf(vgdev, entry);
	}
	wake_up(&vgdev->ctrlq.ack_queue);
}

void virtgpu_dequeue_cursor_func(struct work_struct *work)
{
	struct virtgpu_device *vgdev = container_of(work, struct virtgpu_device,
						    cursorq.dequeue_work);
	struct virtqueue *vq = vgdev->cursorq.vq;
	unsigned int len;
	spin_lock(&vgdev->cursorq.qlock);
	do {
		virtqueue_disable_cb(vgdev->cursorq.vq);
		while (virtqueue_get_buf(vq, &len)) {
		}
	} while (!virtqueue_enable_cb(vgdev->cursorq.vq));
	spin_unlock(&vgdev->cursorq.qlock);
	wake_up(&vgdev->cursorq.ack_queue);
}

void virtgpu_dequeue_event_func(struct work_struct *work)
{
	struct virtgpu_device *vgdev = container_of(work, struct virtgpu_device,
						    eventq.dequeue_work);
	struct virtqueue *vq = vgdev->eventq.vq;
	struct virtgpu_vbuffer *vbuf;
	struct list_head reclaim_list;
	unsigned int len;
	struct virtgpu_vbuffer *entry, *tmp;

	INIT_LIST_HEAD(&reclaim_list);
	spin_lock(&vgdev->eventq.qlock);
	do {
		virtqueue_disable_cb(vgdev->eventq.vq);
		while ((vbuf = virtqueue_get_buf(vq, &len))) {
			list_add(&vbuf->destroy_list, &reclaim_list);
		}
	} while (!virtqueue_enable_cb(vgdev->eventq.vq));
	spin_unlock(&vgdev->eventq.qlock);


	list_for_each_entry_safe(entry, tmp, &reclaim_list, destroy_list) {
		if (entry->resp_cb)
			entry->resp_cb(vgdev, entry);

		spin_lock(&vgdev->eventq.qlock);
		if (add_inbuf(vgdev->eventq.vq, entry) < 0) {
			DRM_ERROR("Error adding buffer to queue\n");
			free_vbuf(vgdev, entry);
		}
		spin_unlock(&vgdev->eventq.qlock);
	}
}

int virtgpu_queue_ctrl_buffer(struct virtgpu_device *vgdev,
			      struct virtgpu_vbuffer *vbuf)
{
	struct virtqueue *vq = vgdev->ctrlq.vq;
	struct scatterlist *sgs[2], vcmd, vout, vresp;
	int outcnt, incnt = 0;
	int ret;

	sg_init_one(&vcmd, vbuf->buf, vbuf->size);
	sgs[0] = &vcmd;
	outcnt = 1;
       
	if (vbuf->vaddr) {
		sg_init_one(&vout, vbuf->vaddr, vbuf->vaddr_len);
		sgs[1] = &vout;
		outcnt++;
	} else if (vbuf->resp_buf) {
		sg_init_one(&vresp, vbuf->resp_buf, vbuf->resp_size);
		sgs[1] = &vresp;
		incnt++;
	}
	spin_lock(&vgdev->ctrlq.qlock);
retry:
	ret = virtqueue_add_sgs(vq, sgs, outcnt, incnt, vbuf, GFP_ATOMIC);
	if (ret == -ENOSPC) {
		spin_unlock(&vgdev->ctrlq.qlock);
		wait_event(vgdev->ctrlq.ack_queue, vq->num_free);
		spin_lock(&vgdev->ctrlq.qlock);
		goto retry;
	} else {
		virtqueue_kick(vq);
	}

	spin_unlock(&vgdev->ctrlq.qlock);

	if (!ret)
		ret = vq->num_free;
	return ret;
}

int virtgpu_queue_cursor(struct virtgpu_device *vgdev)
{
	struct virtqueue *vq = vgdev->cursorq.vq;
	struct scatterlist *sgs[1], ccmd;
	int ret;
	int outcnt;

	sg_init_one(&ccmd, vgdev->cursor_page, sizeof(struct virtgpu_hw_cursor_page));
	sgs[0] = &ccmd;
	outcnt = 1;

	spin_lock(&vgdev->cursorq.qlock);
retry:
	ret = virtqueue_add_sgs(vq, sgs, outcnt, 0, vgdev->cursor_page, GFP_ATOMIC);
	if (ret == -ENOSPC) {
		spin_unlock(&vgdev->cursorq.qlock);
		wait_event(vgdev->cursorq.ack_queue, vq->num_free);
		spin_lock(&vgdev->cursorq.qlock);
		goto retry;
	} else {
		virtqueue_kick(vq);
	}

	spin_unlock(&vgdev->cursorq.qlock);

	if (!ret)
		ret = vq->num_free;
	return ret;
}

/* just create gem objects for userspace and long lived objects,
   just use dma_alloced pages for the queue objects? */

/* create a basic resource */
int virtgpu_cmd_create_resource(struct virtgpu_device *vgdev,
				uint32_t resource_id,
				uint32_t format,
				uint32_t width,
				uint32_t height)
{
	struct virtgpu_command *cmd_p;
	struct virtgpu_vbuffer *vbuf;

	cmd_p = virtgpu_alloc_cmd(vgdev, &vbuf);
	memset(cmd_p, 0, sizeof(*cmd_p));

	cmd_p->type = VIRTGPU_CMD_RESOURCE_CREATE_2D;
	cmd_p->u.resource_create_2d.resource_id = resource_id;
	cmd_p->u.resource_create_2d.format = format;
	cmd_p->u.resource_create_2d.width = width;
	cmd_p->u.resource_create_2d.height = height;

	virtgpu_queue_ctrl_buffer(vgdev, vbuf);
	       
	return 0;
}

int virtgpu_cmd_unref_resource(struct virtgpu_device *vgdev,
			       uint32_t resource_id)
{
	struct virtgpu_command *cmd_p;
	struct virtgpu_vbuffer *vbuf;

	cmd_p = virtgpu_alloc_cmd(vgdev, &vbuf);
	memset(cmd_p, 0, sizeof(*cmd_p));

	cmd_p->type = VIRTGPU_CMD_RESOURCE_UNREF;
	cmd_p->u.resource_unref.resource_id = resource_id;

	virtgpu_queue_ctrl_buffer(vgdev, vbuf);
	       
	return 0;
}

int virtgpu_cmd_resource_inval_backing(struct virtgpu_device *vgdev,
				       uint32_t resource_id)
{
	struct virtgpu_command *cmd_p;
	struct virtgpu_vbuffer *vbuf;

	cmd_p = virtgpu_alloc_cmd(vgdev, &vbuf);
	memset(cmd_p, 0, sizeof(*cmd_p));

	cmd_p->type = VIRTGPU_CMD_RESOURCE_INVAL_BACKING;
	cmd_p->u.resource_inval_backing.resource_id = resource_id;

	virtgpu_queue_ctrl_buffer(vgdev, vbuf);
	       
	return 0;
}

int virtgpu_cmd_set_scanout(struct virtgpu_device *vgdev,
			    uint32_t scanout_id, uint32_t resource_id,
			    uint32_t width, uint32_t height,
			    uint32_t x, uint32_t y)
{
	struct virtgpu_command *cmd_p;
	struct virtgpu_vbuffer *vbuf;

	cmd_p = virtgpu_alloc_cmd(vgdev, &vbuf);
	memset(cmd_p, 0, sizeof(*cmd_p));

	cmd_p->type = VIRTGPU_CMD_SET_SCANOUT;
	cmd_p->u.set_scanout.resource_id = resource_id;
	cmd_p->u.set_scanout.scanout_id = scanout_id;
	cmd_p->u.set_scanout.width = width;
	cmd_p->u.set_scanout.height = height;
	cmd_p->u.set_scanout.x = x;
	cmd_p->u.set_scanout.y = y;

	virtgpu_queue_ctrl_buffer(vgdev, vbuf);
	return 0;
}

int virtgpu_cmd_resource_flush(struct virtgpu_device *vgdev,
			       uint32_t resource_id,
			       uint32_t width, uint32_t height,
			       uint32_t x, uint32_t y)
{
	struct virtgpu_command *cmd_p;
	struct virtgpu_vbuffer *vbuf;

	cmd_p = virtgpu_alloc_cmd(vgdev, &vbuf);
	memset(cmd_p, 0, sizeof(*cmd_p));

	cmd_p->type = VIRTGPU_CMD_RESOURCE_FLUSH;
	cmd_p->u.resource_flush.resource_id = resource_id;
	cmd_p->u.resource_flush.width = width;
	cmd_p->u.resource_flush.height = height;
	cmd_p->u.resource_flush.x = x;
	cmd_p->u.resource_flush.y = y;

	virtgpu_queue_ctrl_buffer(vgdev, vbuf);

	return 0;
}

int virtgpu_cmd_transfer_to_host_2d(struct virtgpu_device *vgdev,
				 uint32_t resource_id, uint32_t offset,
				 uint32_t width, uint32_t height,
				 uint32_t x, uint32_t y)
{
	struct virtgpu_command *cmd_p;
	struct virtgpu_vbuffer *vbuf;

	cmd_p = virtgpu_alloc_cmd(vgdev, &vbuf);
	memset(cmd_p, 0, sizeof(*cmd_p));

	cmd_p->type = VIRTGPU_CMD_TRANSFER_TO_HOST_2D;
	cmd_p->u.transfer_to_host_2d.resource_id = resource_id;
	cmd_p->u.transfer_to_host_2d.offset = offset;
	cmd_p->u.transfer_to_host_2d.width = width;
	cmd_p->u.transfer_to_host_2d.height = height;
	cmd_p->u.transfer_to_host_2d.x = x;
	cmd_p->u.transfer_to_host_2d.y = y;

	virtgpu_queue_ctrl_buffer(vgdev, vbuf);
	       
	return 0;
}

int virtgpu_cmd_resource_attach_backing(struct virtgpu_device *vgdev, uint32_t resource_id, uint32_t nents,
					void *vaddr, dma_addr_t dma_addr, uint32_t vsize)
{
	struct virtgpu_command *cmd_p;
	struct virtgpu_vbuffer *vbuf;

	cmd_p = virtgpu_alloc_cmd(vgdev, &vbuf);
	memset(cmd_p, 0, sizeof(*cmd_p));

	vbuf->vaddr = vaddr;
	vbuf->vaddr_len = vsize;
	vbuf->busaddr = dma_addr;

	cmd_p->type = VIRTGPU_CMD_RESOURCE_ATTACH_BACKING;
	cmd_p->u.resource_attach_backing.resource_id = resource_id;
	cmd_p->u.resource_attach_backing.nr_entries = nents;
	virtgpu_queue_ctrl_buffer(vgdev, vbuf);
	       
	return 0;
}

void virtgpu_cmd_get_display_info_cb(struct virtgpu_device *vgdev,
				     struct virtgpu_vbuffer *vbuf)
{
	spin_lock(&vgdev->display_info_lock);
	memcpy(&vgdev->display_info, vbuf->resp_buf, vbuf->resp_size);
	spin_unlock(&vgdev->display_info_lock);
	wake_up(&vgdev->resp_wq);
}

int virtgpu_cmd_get_display_info(struct virtgpu_device *vgdev)
{
	struct virtgpu_command *cmd_p;
	struct virtgpu_vbuffer *vbuf;

	cmd_p = virtgpu_alloc_cmd_resp(vgdev, &virtgpu_cmd_get_display_info_cb, &vbuf);
	memset(cmd_p, 0, sizeof(*cmd_p));

	cmd_p->type = VIRTGPU_CMD_GET_DISPLAY_INFO;
	virtgpu_queue_ctrl_buffer(vgdev, vbuf);
	return 0;
}

int virtgpu_cmd_context_create(struct virtgpu_device *vgdev, uint32_t id,
			       uint32_t nlen, const char *name)
{
	struct virtgpu_command *cmd_p;
	struct virtgpu_vbuffer *vbuf;

	cmd_p = virtgpu_alloc_cmd(vgdev, &vbuf);
	memset(cmd_p, 0, sizeof(*cmd_p));

	cmd_p->type = VIRTGPU_CMD_CTX_CREATE;
	cmd_p->u.ctx_create.ctx_id = id;
	cmd_p->u.ctx_create.nlen = nlen;
	strncpy(cmd_p->u.ctx_create.debug_name, name, 63);
	cmd_p->u.ctx_create.debug_name[63] = 0;
	virtgpu_queue_ctrl_buffer(vgdev, vbuf);
	return 0;
}

int virtgpu_cmd_context_destroy(struct virtgpu_device *vgdev, uint32_t id)
{
	struct virtgpu_command *cmd_p;
	struct virtgpu_vbuffer *vbuf;

	cmd_p = virtgpu_alloc_cmd(vgdev, &vbuf);
	memset(cmd_p, 0, sizeof(*cmd_p));

	cmd_p->type = VIRTGPU_CMD_CTX_DESTROY;
	cmd_p->u.ctx_destroy.ctx_id = id;
	virtgpu_queue_ctrl_buffer(vgdev, vbuf);
	return 0;
}

int virtgpu_object_attach(struct virtgpu_device *vgdev, struct virtgpu_object *obj, uint32_t resource_id)
{
	uint32_t sz;
	void *vaddr;
	dma_addr_t busaddr;
	int si;
	struct scatterlist *sg;

	if (!obj->pages) {
		int ret;
		ret = virtgpu_object_get_sg_table(vgdev, obj);
		if (ret)
			return ret;
	}

	sz = obj->pages->nents * sizeof(struct virtgpu_mem_entry);
	
	vaddr = dma_alloc_coherent(NULL, sz, &busaddr, GFP_KERNEL | __GFP_COMP);
	if (!vaddr) {
		printk("failed to allocate dma %d\n", sz);
		return -ENOMEM;
	}

	for_each_sg(obj->pages->sgl, sg, obj->pages->nents, si) {
		struct virtgpu_mem_entry *ent = ((struct virtgpu_mem_entry *)vaddr) + si;

		ent->addr = sg_phys(sg);
		ent->length = sg->length;
		ent->pad = 0;
	}

	virtgpu_cmd_resource_attach_backing(vgdev, resource_id, obj->pages->nents, vaddr, busaddr, sz);

	obj->hw_res_handle = resource_id;
	return 0;
}

void virtgpu_cursor_ping(struct virtgpu_device *vgdev)
{
	virtgpu_queue_cursor(vgdev);
}

void virtgpu_event_cb(struct virtgpu_device *vgdev,
		      struct virtgpu_vbuffer *vbuf)
{
	struct virtgpu_event *event;

	event = (struct virtgpu_event *)vbuf->resp_buf;

	DRM_INFO("drm got event cb %d\n", event->type);
	if (event->type == VIRTGPU_EVENT_DISPLAY_CHANGE) {
		spin_lock(&vgdev->display_info_lock);
		memcpy(&vgdev->display_info, &event->u.display_info, sizeof(struct virtgpu_display_info));
		DRM_INFO("enabled displays %d %d\n", vgdev->display_info.pmodes[0].enabled, vgdev->display_info.pmodes[1].enabled);
		spin_unlock(&vgdev->display_info_lock);
		drm_helper_hpd_irq_event(vgdev->ddev);
	}
}

int virtgpu_fill_event_vq(struct virtgpu_device *vgdev, int entries)
{
	struct virtgpu_vbuffer *vbuf;
	int i;
	int ret;
	for (i = 0; i < entries; i++) {
		vbuf = virtgpu_allocate_vbuf(vgdev, 0, sizeof(struct virtgpu_event), virtgpu_event_cb);
		if (!vbuf)
			break;

		spin_lock_irq(&vgdev->eventq.qlock);
		
		ret = add_inbuf(vgdev->eventq.vq, vbuf);
		if (ret < 0) {
			free_vbuf(vgdev, vbuf);
			spin_unlock_irq(&vgdev->eventq.qlock);
			break;
		}
		spin_unlock_irq(&vgdev->eventq.qlock);
	}
	return i;
		
}
