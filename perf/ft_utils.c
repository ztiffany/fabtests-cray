/*
 * Copyright (c) 2015 Cray Inc.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AWV
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <sys/time.h>
#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <assert.h>
#include <malloc.h>
#include <sched.h>
#include <sys/utsname.h>
#include <dlfcn.h>

#include "pmi.h"
#include "ft_utils.h"

extern int PMI_Init(int *) __attribute ((weak));
extern int PMI_Finalize(void) __attribute ((weak));
extern int PMI_Barrier(void) __attribute ((weak));
extern int PMI_Get_size(int *) __attribute ((weak));
extern int PMI_Get_rank(int *) __attribute ((weak));
extern int PMI_Abort(int, const char *) __attribute ((weak));

static void allgather(void *in, void *out, int len)
{
	static int *ivec_ptr=NULL, already_called=0, job_size=0;
	int i, rc;
	int my_rank;
	char *tmp_buf, *out_ptr;

	if (!already_called) {
		rc = PMI_Get_size(&job_size);
		assert(rc == PMI_SUCCESS);
		rc = PMI_Get_rank(&my_rank);
		assert(rc == PMI_SUCCESS);

		ivec_ptr = (int *)malloc(sizeof(int) * job_size);
		assert(ivec_ptr != NULL);

		rc = PMI_Allgather(&my_rank, ivec_ptr, sizeof(int));
		assert(rc == PMI_SUCCESS);

		already_called = 1;
	}

	tmp_buf = (char *)malloc(job_size * len);
	assert(tmp_buf);

	rc = PMI_Allgather(in, tmp_buf, len);
	assert(rc == PMI_SUCCESS);

	out_ptr = out;

	for (i=0; i<job_size; i++) {
		memcpy(&out_ptr[len * ivec_ptr[i]], &tmp_buf[i * len], len);
	}

	free(tmp_buf);
}


void FT_Exit(void)
{
	PMI_Abort(0, "Terminating application successfully");
}

void FT_Abort(void)
{
	PMI_Abort(-1, "pmi abort called");
}

void FT_Barrier(void)
{
	int rc;
	rc = PMI_Barrier();
	assert(rc == PMI_SUCCESS);
}

void FT_Init(int *argc, char ***argv)
{
	int rc, first_spawned;
	void *libpmi_handle;

	libpmi_handle = dlopen("libpmi.so.0", RTLD_LAZY | RTLD_GLOBAL);
	if (libpmi_handle == NULL) {
		perror("Unabled to open libpmi.so check your LD_LIBRARY_PATH");
		abort();
	}

	rc = PMI_Init(&first_spawned);
	assert(rc == PMI_SUCCESS);
}

void FT_Rank(int *rank)
{
	int rc;
	rc = PMI_Get_rank(rank);
	assert(rc == PMI_SUCCESS);
}

void FT_Npes(int *npes)
{
	int rc;
	rc = PMI_Get_numpes_on_smp(npes);
	assert(rc == PMI_SUCCESS);
}

void FT_Finalize(void)
{
	PMI_Finalize();
}

void FT_Job_size(int *nranks)
{
	int rc;
	rc = PMI_Get_size(nranks);
	assert(rc == PMI_SUCCESS);
}

void FT_Allgather(void *src, size_t len_per_rank, void *targ)
{
	allgather(src,targ,len_per_rank);
}

void FT_Bcast(void *buffer, size_t len)
{
	int rc;
	rc = PMI_Bcast(buffer,len);
	assert(rc == PMI_SUCCESS);
}

