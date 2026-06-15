/* SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <inttypes.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/types.h>

#include "dm_coh_memory.h"
#include "perftest_parameters.h"


#define DC_DEV "/dev/dma_heap/coh"

struct dc_alloc_data {
	__u64 len;
	__u32 fd;
	__u32 fd_flags;
	__u64 reserved;
};

#define DC_IOC_MAGIC 'S'
#define DC_IOC_ALLOC _IOWR(DC_IOC_MAGIC, 0, struct dc_alloc_data)

struct dmabuf_coh_memory_ctx {
	struct memory_ctx base;
	uint64_t alloc_size;
};

static int dmabuf_coh_memory_init(struct memory_ctx *ctx)
{
	(void)ctx;
	return SUCCESS;
}

static int dmabuf_coh_memory_destroy(struct memory_ctx *ctx)
{
	free(container_of(ctx, struct dmabuf_coh_memory_ctx, base));
	return SUCCESS;
}

int dmabuf_coh_alloc_region(uint64_t size, int *fd_out, void **addr_out)
{
	struct dc_alloc_data req = {
		.len      = size,
		.fd_flags = O_RDWR | O_CLOEXEC,
		.reserved = 0,
	};
	int sb_fd;

	sb_fd = open(DC_DEV, O_RDWR);
	if (sb_fd < 0) {
		fprintf(stderr, "dmabuf: open(%s) failed: %s\n", DC_DEV, strerror(errno));
		return FAILURE;
	}

	if (ioctl(sb_fd, DC_IOC_ALLOC, &req) < 0) {
		fprintf(stderr, "dmabuf: DC_IOC_ALLOC failed (size=%" PRIu64 "): %s\n",
			size, strerror(errno));
		close(sb_fd);
		return FAILURE;
	}
	close(sb_fd);

	*fd_out = (int)req.fd;
	*addr_out = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, *fd_out, 0);
	if (*addr_out == MAP_FAILED) {
		fprintf(stderr, "dmabuf: mmap failed: %s\n", strerror(errno));
		close(*fd_out);
		*fd_out = 0;
		*addr_out = NULL;
		return FAILURE;
	}

	memset(*addr_out, 0, size);
	return SUCCESS;
}

void dmabuf_coh_free_region(int fd, void *addr, uint64_t size)
{
	if (addr)
		munmap(addr, size);
	if (fd > 0)
		close(fd);
}

static int dmabuf_coh_memory_allocate_buffer(struct memory_ctx *ctx, int alignment,
					 uint64_t size, int *dmabuf_fd,
					 uint64_t *dmabuf_offset, void **addr,
					 bool *can_init)
{
	struct dmabuf_coh_memory_ctx *dctx = container_of(ctx, struct dmabuf_coh_memory_ctx, base);

	(void)alignment;

	if (dmabuf_coh_alloc_region(size, dmabuf_fd, addr) != SUCCESS)
		return FAILURE;

	*dmabuf_offset = 0;
	dctx->alloc_size = size;
	*can_init = true;
	return SUCCESS;
}

static int dmabuf_coh_memory_free_buffer(struct memory_ctx *ctx, int dmabuf_fd,
				     void *addr, uint64_t size)
{
	(void)ctx;
	(void)dmabuf_fd;
	if (addr)
		munmap(addr, size);
	return SUCCESS;
}

struct memory_ctx *dmabuf_coh_memory_create(struct perftest_parameters *params)
{
	struct dmabuf_coh_memory_ctx *ctx;

	(void)params;
	ALLOCATE(ctx, struct dmabuf_coh_memory_ctx, 1);
	ctx->base.init = dmabuf_coh_memory_init;
	ctx->base.destroy = dmabuf_coh_memory_destroy;
	ctx->base.allocate_buffer = dmabuf_coh_memory_allocate_buffer;
	ctx->base.free_buffer = dmabuf_coh_memory_free_buffer;
	ctx->base.copy_host_to_buffer = memcpy;
	ctx->base.copy_buffer_to_host = memcpy;
	ctx->base.copy_buffer_to_buffer = memcpy;
	ctx->alloc_size = 0;
	return &ctx->base;
}
