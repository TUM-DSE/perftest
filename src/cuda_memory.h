/* SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause */
/*
 * Copyright 2023 Amazon.com, Inc. or its affiliates. All rights reserved.
 */

#ifndef CUDA_MEMORY_H
#define CUDA_MEMORY_H

#include "memory.h"
#include "config.h"


struct perftest_parameters;

bool cuda_memory_supported();

bool cuda_memory_dmabuf_supported();

bool data_direct_supported();

bool cuda_gpu_touch_supported();


struct memory_ctx *cuda_memory_create(struct perftest_parameters *params);


#ifndef HAVE_CUDA

inline bool cuda_memory_supported() {
	return false;
}

inline bool cuda_memory_dmabuf_supported() {
	return false;
}

inline bool data_direct_supported() {
	return false;
}

inline struct memory_ctx *cuda_memory_create(struct perftest_parameters *params) {
	return NULL;
}

inline bool cuda_gpu_touch_supported() {
	return false;
}

#endif


/* --- Bounce-buffer API -------------------------------------------------- */

#ifdef HAVE_CUDA

/*
 * cuda_bounce_memory_ctx - extends cuda_memory_ctx with a pinned host
 * bounce buffer.  The GPU buffer (cuMemAlloc) is used as the data source;
 * the pinned host buffer (cuMemAllocHost) is registered as the RDMA MR so
 * no GPUDirect / nvidia_peermem is required.
 *
 * Layout in memory (container_of works because bounce_base embeds base):
 *
 *   struct cuda_bounce_memory_ctx {
 *       struct cuda_memory_ctx  cuda;   <-- 'base' is cuda.base
 *       void                   *gpu_ptr;
 *       void                   *bounce_ptr;
 *       uint64_t                bounce_size;
 *   };
 */
struct cuda_bounce_memory_ctx {
	struct cuda_memory_ctx  cuda;        /* must be first */
	void                   *gpu_ptr;     /* cuMemAlloc device buffer      */
	void                   *bounce_ptr;  /* cuMemAllocHost pinned buffer   */
	uint64_t                bounce_size;
};

/*
 * cuda_bounce_memory_create - allocate and initialise a bounce-buffer
 * memory context.  Returns a pointer to the embedded memory_ctx base,
 * compatible with the standard memory_create interface.
 */
struct memory_ctx *cuda_bounce_memory_create(struct perftest_parameters *params);

/*
 * cuda_bounce_copy - synchronous GPU->host copy for one QP iteration.
 * Must be called (on the server) before each RDMA post so the NIC reads
 * up-to-date data from the pinned host bounce buffer.
 * Uses cuMemcpy (driver API, unified addressing) — no cudart dependency.
 */
int cuda_bounce_copy(struct cuda_bounce_memory_ctx *bctx, uint64_t size);

bool cuda_bounce_memory_supported(void);

#else /* !HAVE_CUDA */

struct cuda_bounce_memory_ctx;   /* opaque forward decl */

static inline struct memory_ctx *
cuda_bounce_memory_create(struct perftest_parameters *params)
{
	(void)params;
	fprintf(stderr, "cuda_bounce: perftest not compiled with CUDA support\n");
	return NULL;
}

static inline int
cuda_bounce_copy(struct cuda_bounce_memory_ctx *b, uint64_t s)
{
	(void)b; (void)s; return FAILURE;
}

static inline bool cuda_bounce_memory_supported(void) { return false; }

#endif /* HAVE_CUDA */

#endif /* CUDA_MEMORY_H */
