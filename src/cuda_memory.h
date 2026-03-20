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



/* ---- Bounce-buffer API -------------------------------------------------- */

#ifdef HAVE_CUDA

/*
 * cuda_bounce_memory_ctx - wraps cuda_memory_ctx with a pinned host bounce
 * buffer.  The client allocates a GPU buffer (cuMemAlloc) as the data source
 * and a pinned host buffer (cuMemAllocHost) as the RDMA MR, so the NIC
 * operates on ordinary DRAM with no GPUDirect / nvidia_peermem required.
 *
 * Must be first member so &bctx->cuda == (void *)bctx, keeping free() and
 * container_of() correct when cuda_memory_init/destroy operate on it.
 */
struct cuda_bounce_memory_ctx {
	struct cuda_memory_ctx  cuda;       /* must be first */
	void                   *gpu_ptr;    /* cuMemAlloc device buffer     */
	void                   *bounce_ptr; /* cuMemAllocHost pinned buffer  */
	uint64_t                bounce_size;
};

struct memory_ctx *cuda_bounce_memory_create(struct perftest_parameters *params);

/*
 * cuda_bounce_copy - synchronous GPU->host copy (DtoH) using the driver-API
 * cuMemcpy (unified addressing; no cudart dependency).  Call on the client
 * before each RDMA post so the NIC reads up-to-date GPU data.
 */
int cuda_bounce_copy(struct cuda_bounce_memory_ctx *bctx, uint64_t size);

bool cuda_bounce_memory_supported(void);

#else  /* !HAVE_CUDA */

struct cuda_bounce_memory_ctx;  /* opaque */

static inline struct memory_ctx *
cuda_bounce_memory_create(struct perftest_parameters *params)
{
	(void)params;
	fprintf(stderr, "cuda_bounce: not compiled with CUDA support\n");
	return NULL;
}

static inline int
cuda_bounce_copy(struct cuda_bounce_memory_ctx *b, uint64_t s)
{ (void)b; (void)s; return FAILURE; }

static inline bool cuda_bounce_memory_supported(void) { return false; }

#endif /* HAVE_CUDA */

#endif /* CUDA_MEMORY_H */
