/* SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause */

#ifndef UVMGPU_MEMORY_H
#define UVMGPU_MEMORY_H

#include "memory.h"
#include "config.h"

struct perftest_parameters;

bool uvmgpu_memory_supported();

struct memory_ctx *uvmgpu_memory_create(struct perftest_parameters *params);

#ifndef HAVE_UVMGPU

inline bool uvmgpu_memory_supported() {
	return false;
}

inline struct memory_ctx *uvmgpu_memory_create(struct perftest_parameters *params) {
	return NULL;
}

#endif

#endif /* UVMGPU_MEMORY_H */
