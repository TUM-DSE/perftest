/* SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause */
#ifndef DMABUF_MEMORY_H
#define DMABUF_MEMORY_H

#include "memory.h"
#include <stdint.h>

struct perftest_parameters;

struct memory_ctx *dmabuf_memory_create(struct perftest_parameters *params);

int dmabuf_alloc_region(uint64_t size, int *fd_out, void **addr_out);
void dmabuf_free_region(int fd, void *addr, uint64_t size);

#endif /* DMABUF_MEMORY_H */
