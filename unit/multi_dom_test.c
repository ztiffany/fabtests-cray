/*
 * Copyright (c) 2013-2014 Intel Corporation.  All rights reserved.
 * Copyright (c) 2014 Cisco Systems, Inc.  All rights reserved.
 * Copyright (c) 2015 Los Alamos National Security, LLC. All rights reserved.
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

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <getopt.h>
#include <poll.h>
#include <time.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <inttypes.h>
#include <assert.h>

#include <rdma/fabric.h>
#include <rdma/fi_domain.h>
#include <rdma/fi_errno.h>
#include <rdma/fi_endpoint.h>
#include <rdma/fi_cm.h>

#include "shared.h"
#include "unit_common.h"

#define MAX_ADDR 256

static struct fid_domain **domains;
static struct fid_ep **eps;

/*
 * Tests:
 * - test open and close of an endpoint
 */

int main(int argc, char **argv)
{
	int op, ret, ndoms = 3, neps = 3, i, j;

	hints = fi_allocinfo();
	if (!hints) {
		exit(1);
	}

	while ((op = getopt(argc, argv, "f:p:d:D:n:d:e:h")) != -1) {
		switch (op) {
		case 'f':
			hints->fabric_attr->name = strdup(optarg);
			break;
		case 'p':
			hints->fabric_attr->prov_name = strdup(optarg);
			break;
		case 'd':
			ndoms = atoi(optarg);
			break;
		case 'e':
			neps = atoi(optarg);
			break;
		case 'h':
		default:
			printf("usage: %s\n", argv[0]);
			printf("\t[-f fabric_name]\n");
			printf("\t[-p provider_name]\n");
			printf("\t[-d ndomains]\n");
			printf("\t[-e neps]\n");
			exit(1);
		}
	}

	hints->mode = ~0;

	ret = fi_getinfo(FI_VERSION(1, 0), NULL, 0, 0, hints, &fi);
	if (ret != 0) {
		printf("fi_getinfo %s\n", fi_strerror(-ret));
		exit(1);
	}

	ret = fi_fabric(fi->fabric_attr, &fabric, NULL);
	if (ret != 0) {
		printf("fi_fabric %s\n", fi_strerror(-ret));
		exit(1);
	}

	domains = (struct fid_domain **)malloc(ndoms * sizeof(struct fid_domain *));
	assert(domains);
	eps = (struct fid_ep **)malloc(neps * ndoms * sizeof(struct fid_ep *));
	assert(eps);

	for (i = 0; i < ndoms; i++) {
		ret = fi_domain(fabric, fi, &domains[i], NULL);
		if (ret != 0) {
			printf("fi_domain %s\n", fi_strerror(-ret));
			exit(1);
		}

		for (j = 0; j < neps; j++) {
			int idx = (i * neps) + j;
			ret = fi_endpoint(domains[i], fi, &eps[idx], NULL);
			if (ret != 0) {
				printf("[%d:%d] ]fi_endpoint %s\n", i, j, fi_strerror(-ret));
				exit(1);
			}
		}
	}

	for (i = 0; i < ndoms; i++) {
		for (j = 0; j < neps; j++) {
			int idx = (i * neps) + j;
			ret = fi_close(&eps[idx]->fid);
			if (ret != 0) {
				printf("Error %d closing ep: %s\n", ret, fi_strerror(-ret));
				exit(1);
			}
		}

		ret = fi_close(&domains[i]->fid);
		if (ret != 0) {
			printf("Error %d closing domain: %s\n", ret, fi_strerror(-ret));
			exit(1);
		}
	}

	free(eps);
	free(domains);

	ret = fi_close(&fabric->fid);
	if (ret != 0) {
		printf("Error %d closing fabric: %s\n", ret, fi_strerror(-ret));
		exit(1);
	}

	fi_freeinfo(fi);
	fi_freeinfo(hints);

	return ret;
}
