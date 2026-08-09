/* SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <uvmgpu.h>

#include "dm_coh_memory.h"
#include "memory.h"
#include "perftest_parameters.h"
#include "uvmgpu_memory.h"

struct uvmgpu_memory_ctx {
  struct memory_ctx base;
  int gpu_index;
  uvmgpu_t *g;
  uvmgpu_buf_t dev;
  uvmgpu_buf_t host;
  int host_fd;
  void *host_addr;
  uint64_t host_total;
  bool dev_armed;
  bool host_armed;
};

static int uvmgpu_memory_init(struct memory_ctx *ctx) {
  struct uvmgpu_memory_ctx *m =
      container_of(ctx, struct uvmgpu_memory_ctx, base);

  if (uvmgpu_open(m->gpu_index, &m->g) != 0) {
    fprintf(stderr, "uvmgpu: uvmgpu_open(gpu=%d) failed: %s\n", m->gpu_index,
            strerror(errno));
    return FAILURE;
  }

  return SUCCESS;
}

static int uvmgpu_memory_destroy(struct memory_ctx *ctx) {
  struct uvmgpu_memory_ctx *m =
      container_of(ctx, struct uvmgpu_memory_ctx, base);

  if (m->g)
    uvmgpu_close(m->g);
  free(m);
  return SUCCESS;
}

static void uvmgpu_release_buffers(struct uvmgpu_memory_ctx *m) {
  if (m->host_armed) {
    uvmgpu_free(m->g, m->host);
    m->host_armed = false;
  }
  if (m->dev_armed) {
    uvmgpu_free(m->g, m->dev);
    m->dev_armed = false;
  }
  if (m->host_addr) {
    dmabuf_coh_free_region(m->host_fd, m->host_addr, m->host_total);
    m->host_addr = NULL;
    m->host_fd = -1;
  }
}

static int uvmgpu_memory_allocate_buffer(struct memory_ctx *ctx, int alignment,
                                         uint64_t size, int *dmabuf_fd,
                                         uint64_t *dmabuf_offset, void **addr,
                                         bool *can_init) {
  struct uvmgpu_memory_ctx *m =
      container_of(ctx, struct uvmgpu_memory_ctx, base);

  (void)alignment;

  m->host_total = uvmgpu_sealed_dtoh_buf_bytes(size);

  if (uvmgpu_alloc_device(m->g, size, &m->dev) != 0) {
    fprintf(stderr, "uvmgpu: alloc device vidmem (%lu B) failed: %s\n",
            (unsigned long)size, strerror(errno));
    goto fail;
  }
  m->dev_armed = true;

  if (dmabuf_coh_alloc_region(m->host_total, &m->host_fd, &m->host_addr) !=
      SUCCESS) {
    fprintf(stderr, "uvmgpu: alloc host dmabuf (%lu B) failed\n",
            (unsigned long)m->host_total);
    goto fail;
  }

  if (uvmgpu_import_nic_dmabuf(m->g, m->host_fd, m->host_total, &m->host) !=
      0) {
    fprintf(stderr, "uvmgpu: import NIC dmabuf (%lu B) failed: %s\n",
            (unsigned long)m->host_total, strerror(errno));
    goto fail;
  }
  m->host_armed = true;

  *addr = m->host_addr;
  *dmabuf_fd = m->host_fd;
  *dmabuf_offset = 0;
  *can_init = false;

  return SUCCESS;

fail:
  uvmgpu_release_buffers(m);
  return FAILURE;
}

static int uvmgpu_memory_free_buffer(struct memory_ctx *ctx, int dmabuf_fd,
                                     void *addr, uint64_t size) {
  struct uvmgpu_memory_ctx *m =
      container_of(ctx, struct uvmgpu_memory_ctx, base);

  (void)dmabuf_fd;
  (void)addr;
  (void)size;

  uvmgpu_release_buffers(m);
  return SUCCESS;
}

static int uvmgpu_copy_from_gpu_to_bounce_buffer(struct memory_ctx *ctx,
						 uintptr_t bounce_buffer,
                                                 size_t size) {
  struct uvmgpu_memory_ctx *m =
      container_of(ctx, struct uvmgpu_memory_ctx, base);

  (void)bounce_buffer;

  if (uvmgpu_copy_to_host_sealed(m->g, m->host, m->dev, size) != 0) {
    fprintf(stderr, "uvmgpu: sealed GPU->sysmem copy (%zu B) failed: %s\n",
            size, strerror(errno));
    return FAILURE;
  }

  return SUCCESS;
}

static int uvmgpu_copy_from_bounce_buffer_to_gpu(struct memory_ctx *ctx,
						 uintptr_t bounce_buffer,
                                                 size_t size) {
  struct uvmgpu_memory_ctx *m =
      container_of(ctx, struct uvmgpu_memory_ctx, base);

  (void)bounce_buffer;

  if (uvmgpu_copy_to_device_sealed(m->g, m->dev, m->host, size) != 0) {
    fprintf(stderr, "uvmgpu: sealed sysmem->GPU copy (%zu B) failed: %s\n",
            size, strerror(errno));
    return FAILURE;
  }

  return SUCCESS;
}

static void *uvmgpu_memory_copy_to_buffer(void *dest, const void *src,
                                          size_t size) {
  return memcpy(dest, src, size);
}

bool uvmgpu_memory_supported() { return true; }

struct memory_ctx *uvmgpu_memory_create(struct perftest_parameters *params) {
  struct uvmgpu_memory_ctx *ctx;

  ALLOCATE(ctx, struct uvmgpu_memory_ctx, 1);
  memset(ctx, 0, sizeof(*ctx));
  ctx->base.init = uvmgpu_memory_init;
  ctx->base.destroy = uvmgpu_memory_destroy;
  ctx->base.allocate_buffer = uvmgpu_memory_allocate_buffer;
  ctx->base.free_buffer = uvmgpu_memory_free_buffer;
  ctx->base.copy_host_to_buffer = uvmgpu_memory_copy_to_buffer;
  ctx->base.copy_buffer_to_host = uvmgpu_memory_copy_to_buffer;
  ctx->base.copy_buffer_to_buffer = uvmgpu_memory_copy_to_buffer;
  ctx->base.copy_from_gpu_to_bounce_buffer =
      uvmgpu_copy_from_gpu_to_bounce_buffer;
  ctx->base.copy_from_bounce_buffer_to_gpu =
      uvmgpu_copy_from_bounce_buffer_to_gpu;
  ctx->gpu_index = params->uvmgpu_device_id;
  ctx->host_fd = -1;

  return &ctx->base;
}
