/* SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause */
#ifndef CVM_SHM_MEMORY_H
#define CVM_SHM_MEMORY_H

#include <stdint.h>

int cvm_shm_alloc_region(uint64_t size, int *fd_out, void **addr_out);
void cvm_shm_free_region(int fd, void *addr, uint64_t size);

#endif /* CVM_SHM_MEMORY_H */
