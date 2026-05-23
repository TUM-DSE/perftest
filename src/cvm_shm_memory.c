/* SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause */

#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <inttypes.h>
#include <sys/mman.h>

#include "cvm_shm_memory.h"
#include "perftest_parameters.h"

#define CVM_SHM_DEV "/dev/cvm_shm"

/*
 * Open /dev/cvm_shm and mmap the pre-allocated decrypted buffer.
 *
 * Unlike dma_heap_coh, cvm_shm does not use DMA-BUF: the kernel module
 * exposes a plain mmap interface over pages that have been marked decrypted
 * via set_memory_decrypted(). When the RDMA driver maps these pages for DMA
 * the kernel will not route them through swiotlb because they are already
 * shared/unencrypted memory.
 */
int cvm_shm_alloc_region(uint64_t size, int *fd_out, void **addr_out)
{
	int fd;
	void *addr;

	fd = open(CVM_SHM_DEV, O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "cvm_shm: open(%s) failed: %s\n",
			CVM_SHM_DEV, strerror(errno));
		return FAILURE;
	}

	addr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (addr == MAP_FAILED) {
		fprintf(stderr, "cvm_shm: mmap failed (size=%" PRIu64 "): %s\n",
			size, strerror(errno));
		close(fd);
		return FAILURE;
	}

	memset(addr, 0, size);
	*fd_out = fd;
	*addr_out = addr;
	return SUCCESS;
}

void cvm_shm_free_region(int fd, void *addr, uint64_t size)
{
	if (addr)
		munmap(addr, size);
	if (fd > 0)
		close(fd);
}
