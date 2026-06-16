/* SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <nvgpu.h>

#include "memory.h"
#include "nvgpu_memory.h"
#include "perftest_parameters.h"

#define NVGPU_TAG_BYTES 4096

/* Report the running sealed-copy rate every this many copies (NVGPU_PROFILE=1). */
#define NVGPU_PROFILE_EVERY 2000

struct nvgpu_memory_ctx {
  struct memory_ctx base;
  int gpu_index;
  nvgpu_t *g;
  nvgpu_mem_t *dev;
  nvgpu_mem_t *host;
  nvgpu_mem_t *tag;
  uint64_t buf_size;
  /* NVGPU_PROFILE: time the GPU->sysmem sealed copy in isolation (no NIC), so we
   * can tell whether the bottleneck is the copy itself or the RDMA side. */
  int profile;
  uint64_t prof_copies;
  uint64_t prof_ns;
  uint64_t prof_bytes;
};

static inline uint64_t nvgpu_now_ns(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000000000ull + ts.tv_nsec;
}

static int nvgpu_memory_init(struct memory_ctx *ctx) {
  struct nvgpu_memory_ctx *m = container_of(ctx, struct nvgpu_memory_ctx, base);

  if (nvgpu_open(m->gpu_index, &m->g) != 0) {
    fprintf(stderr, "nvgpu: nvgpu_open(gpu=%d) failed: %s\n", m->gpu_index,strerror(errno));
    return FAILURE;
  }

  /* nvgpu_open already created the CE and ran the one-time WLC bootstrap, so the first copy in
   * the timed loop is warm -- no ~300 ms first-copy stall to drag the BW average below peak. */
  return SUCCESS;
}

static int nvgpu_memory_destroy(struct memory_ctx *ctx) {
  struct nvgpu_memory_ctx *m = container_of(ctx, struct nvgpu_memory_ctx, base);

  if (m->profile && m->prof_copies > 0 && m->prof_ns > 0) {
    fprintf(stderr,
            "nvgpu_profile: final %llu copies: %.1f us/copy, %.2f GB/s (GPU->sysmem only)\n",
            (unsigned long long)m->prof_copies,
            (double)m->prof_ns / m->prof_copies / 1000.0,
            (double)m->prof_bytes / m->prof_ns);
  }

  if (m->g)
    nvgpu_close(m->g);
  free(m);
  return SUCCESS;
}

static void nvgpu_release_buffers(struct nvgpu_memory_ctx *m) {
  if (m->tag) {
    nvgpu_free(m->tag);
    m->tag = NULL;
  }
  if (m->host) {
    nvgpu_free(m->host);
    m->host = NULL;
  }
  if (m->dev) {
    nvgpu_free(m->dev);
    m->dev = NULL;
  }
}

static int nvgpu_memory_allocate_buffer(struct memory_ctx *ctx, int alignment,
                                        uint64_t size, int *dmabuf_fd,
                                        uint64_t *dmabuf_offset, void **addr,
                                        bool *can_init) {
  struct nvgpu_memory_ctx *m = container_of(ctx, struct nvgpu_memory_ctx, base);

  (void)alignment;

  if (nvgpu_alloc_device(m->g, size, &m->dev) != 0) {
    fprintf(stderr, "nvgpu: alloc device vidmem (%lu B) failed: %s\n",(unsigned long)size, strerror(errno));
    goto fail;
  }
  if (nvgpu_alloc_host(m->g, size, &m->host) != 0) {
    fprintf(stderr, "nvgpu: alloc host ciphertext (%lu B) failed: %s\n",(unsigned long)size, strerror(errno));
    goto fail;
  }
  if (nvgpu_alloc_host(m->g, NVGPU_TAG_BYTES, &m->tag) != 0) {
    fprintf(stderr, "nvgpu: alloc tag scratch failed: %s\n", strerror(errno));
    goto fail;
  }

  void *host_ptr = nvgpu_host_ptr(m->host);
  int fd = nvgpu_host_dmabuf_fd(m->host);
  if (!host_ptr || fd < 0) {
    fprintf(stderr,"nvgpu: host buffer is not dmabuf-exportable (ptr=%p fd=%d)\n",host_ptr, fd);
    goto fail;
  }

  m->buf_size = size;
  *addr = host_ptr;
  *dmabuf_fd = fd;
  *dmabuf_offset = 0;
  *can_init =false;

  return SUCCESS;

fail:
  nvgpu_release_buffers(m);
  return FAILURE;
}

static int nvgpu_memory_free_buffer(struct memory_ctx *ctx, int dmabuf_fd,void *addr, uint64_t size) {
  struct nvgpu_memory_ctx *m = container_of(ctx, struct nvgpu_memory_ctx, base);

  (void)dmabuf_fd;
  (void)addr;
  (void)size;

  nvgpu_release_buffers(m);
  return SUCCESS;
}

static int nvgpu_copy_from_gpu_to_bounce_buffer(struct memory_ctx *ctx,size_t size) {
  struct nvgpu_memory_ctx *m = container_of(ctx, struct nvgpu_memory_ctx, base);

  uint64_t t0 = m->profile ? nvgpu_now_ns() : 0;

  if (nvgpu_copy_to_host_sealed(m->g, m->host, m->tag, m->dev, size) != 0) {
    fprintf(stderr, "nvgpu: sealed GPU->sysmem copy (%zu B) failed: %s\n", size,strerror(errno));
    return FAILURE;
  }

  if (m->profile) {
    m->prof_ns += nvgpu_now_ns() - t0;
    m->prof_bytes += size;
    if (++m->prof_copies >= NVGPU_PROFILE_EVERY) {
      /* bytes/ns == GB/s. This is the GPU->sysmem copy rate only (the NIC DMA is
       * separate), so it tells us if the sealed copy is the bottleneck. */
      fprintf(stderr,
              "nvgpu_profile: sealed copy %zu B: %.1f us/copy, %.2f GB/s (GPU->sysmem only)\n",
              size, (double)m->prof_ns / m->prof_copies / 1000.0,
              (double)m->prof_bytes / m->prof_ns);
      m->prof_copies = 0;
      m->prof_ns = 0;
      m->prof_bytes = 0;
    }
  }
  return SUCCESS;
}

static void *nvgpu_memory_copy_to_buffer(void *dest, const void *src,size_t size) {
  return memcpy(dest, src, size);
}

bool nvgpu_memory_supported() { return true; }

struct memory_ctx *nvgpu_memory_create(struct perftest_parameters *params) {
  struct nvgpu_memory_ctx *ctx;

  ALLOCATE(ctx, struct nvgpu_memory_ctx, 1);
  memset(ctx, 0, sizeof(*ctx));
  ctx->base.init = nvgpu_memory_init;
  ctx->base.destroy = nvgpu_memory_destroy;
  ctx->base.allocate_buffer = nvgpu_memory_allocate_buffer;
  ctx->base.free_buffer = nvgpu_memory_free_buffer;
  ctx->base.copy_host_to_buffer = nvgpu_memory_copy_to_buffer;
  ctx->base.copy_buffer_to_host = nvgpu_memory_copy_to_buffer;
  ctx->base.copy_buffer_to_buffer = nvgpu_memory_copy_to_buffer;
  ctx->base.copy_from_gpu_to_bounce_buffer = nvgpu_copy_from_gpu_to_bounce_buffer;
  ctx->gpu_index = params->nvgpu_device_id;
  ctx->profile = (getenv("NVGPU_PROFILE") != NULL);

  return &ctx->base;
}
