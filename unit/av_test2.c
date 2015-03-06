/*
 * Copyright (c) 2013-2014 Intel Corporation.  All rights reserved.
 * Copyright (c) 2014 Cisco Systems, Inc.  All rights reserved.
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

struct fi_info *hints = NULL;
static struct fi_fabric_attr fabric_hints;
static struct fi_eq_attr eq_attr;

static struct fi_info *fi = NULL;
static struct fid_fabric *fabric = NULL;
static struct fid_domain *domain = NULL;
static struct fid_eq *eq = NULL;
static struct fid_ep *ep = NULL;

int async_avail = 1;  /* be optimistic */
int test_map_type = 0;
int test_table_type = 0;
int num_entries = 16;

static int
av_test_sync(enum fi_av_type type, int count, uint64_t flags)
{
	int i;
	int ret;
	struct fi_av_attr attr;
	struct fid_av *av;
	uint8_t *cptr;
	void *local_name_addr = NULL, *remote_name_addr = NULL;
	void *test_name_addr = NULL;
	static fi_addr_t *fi_addr_vec, remote_fi_addr;
	size_t addrlen = 0,test_addrlen = 0;

	memset(&attr, 0, sizeof(attr));
	attr.type = type;
	attr.count = count;
	attr.flags = flags;

	ret = fi_av_open(domain, &attr, &av, NULL);
	if (ret != FI_SUCCESS) {
		fprintf(stderr,"fi_av_open(%d, %s) = %d, %s\n",
				count, fi_tostr(&type, FI_TYPE_AV_TYPE),
				ret, fi_strerror(-ret));
		goto err;
	}

	ret = fi_getname(&ep->fid, local_name_addr, &addrlen);
	if (ret != -FI_ETOOSMALL) {
		fprintf(stderr,"fi_getname %d: %s\n", ret, fi_strerror(-ret));
		goto err;
	}

	local_name_addr = malloc(addrlen);
	assert(local_name_addr);

	ret = fi_getname(&ep->fid, local_name_addr, &addrlen);
	if (ret != FI_SUCCESS) {
		fprintf(stderr,"fi_getname %d: %s\n", ret, fi_strerror(-ret));
		goto err;
	}

	/*
	 * call these remote since in a real app there would have been
	 * some kind of out of band exchange of the local addrs
	 */

	remote_name_addr = malloc(addrlen * num_entries);
	assert(remote_name_addr);

	cptr = (uint8_t *)remote_name_addr;
	for (i = 0 ;i < num_entries; i++) {
		memcpy(cptr,local_name_addr,addrlen);
		cptr += addrlen;
	}

	/*
	 * let's insert one entry first
	 */

	ret = fi_av_insert(av, remote_name_addr, 1, &remote_fi_addr,
				0, NULL);
	if (ret != 1) {
		fprintf(stderr,"fi_av_insert %d: %s\n", ret, fi_strerror(-ret));
		goto err;
	}

	/*
	 * lets try to read it back
	 */

	ret = fi_av_lookup(av, remote_fi_addr, test_name_addr, &test_addrlen);
	if (ret != -FI_ETOOSMALL) {
		fprintf(stderr,"fi_av_lookup %d: %s\n", ret, fi_strerror(-ret));
		goto err;
	}

	test_name_addr = malloc(test_addrlen);
	assert(test_name_addr != NULL);

	ret = fi_av_lookup(av, remote_fi_addr, test_name_addr, &test_addrlen);
	if (ret != FI_SUCCESS) {
		fprintf(stderr,"fi_av_lookup %d: %s\n", ret, fi_strerror(-ret));
		goto err;
	}

	if (memcmp(local_name_addr, test_name_addr, test_addrlen)) {
		fprintf(stderr,"fi_av wrong answer\n");
		ret = -FI_EINVAL;
		goto err;
	}

	ret = fi_av_remove(av, &remote_fi_addr, 1, 0);
	if (ret != FI_SUCCESS) {
		fprintf(stderr,"fi_av_remove %d: %s\n", ret, fi_strerror(-ret));
		goto err;
	}

	/*
	 * now lets try adding some more
	 */

	fi_addr_vec = (fi_addr_t *)malloc(num_entries * sizeof(fi_addr_t));
	assert(fi_addr_vec != NULL);

	ret = fi_av_insert(av, remote_name_addr, num_entries, fi_addr_vec,
				0, NULL);
	if (ret != num_entries) {
		fprintf(stderr,"fi_av_insert %d: %s\n", ret, fi_strerror(-ret));
		goto err;
	}

err:
	if (av != NULL) {
		ret = fi_close(&av->fid);
		if (ret != FI_SUCCESS) {
			fprintf(stderr,"fi_av_close %d: %s\n", ret,
				fi_strerror(-ret));
			goto err;
		}
	}
	return ret;
}

/*
 * Tests:
 * - test open and close of a domain
 */

int main(int argc, char **argv)
{
	int op, ret;
	int test_map_type,test_table_type;

	hints = fi_allocinfo();
	if (hints == NULL)
		exit(EXIT_FAILURE);

	while ((op = getopt(argc, argv, "f:p:d:n:")) != -1) {
		switch (op) {
		case 'f':
			fabric_hints.name = optarg;
			break;
		case 'n':
			num_entries = atoi(optarg);
			break;
		case 'p':
			fabric_hints.prov_name = optarg;
			break;
		default:
			printf("usage: %s\n", argv[0]);
			printf("\t[-f fabric_name]\n");
			printf("\t[-p provider_name]\n");
			printf("\t[-n num av entries]\n");
			exit(EXIT_FAILURE);
		}
	}

	hints->fabric_attr = &fabric_hints;
	hints->mode = ~0;
	hints->ep_attr->type = FI_EP_RDM;

	ret = fi_getinfo(FI_VERSION(1, 0), NULL, 0, 0, hints, &fi);
	if (ret != 0) {
		printf("fi_getinfo %s\n", fi_strerror(-ret));
		goto err;
	}

	ret = fi_fabric(fi->fabric_attr, &fabric, NULL);
	if (ret != 0) {
		printf("fi_fabric %s\n", fi_strerror(-ret));
		goto err;
	}

	domain = calloc(1,sizeof (struct fid_domain));
	if (domain == NULL) {
		perror("malloc");
		goto err;
	}

	ret = fi_domain(fabric, fi, &domain, NULL);
	if (ret != FI_SUCCESS) {
		printf("fi_domain %s\n", fi_strerror(-ret));
		goto err;
	}

	/*
 	 * see if we can open an eq on this fabric
 	 */

	eq_attr.size = 1024;
	eq_attr.wait_obj = FI_WAIT_UNSPEC;
	ret = fi_eq_open(fabric, &eq_attr, &eq, NULL);
	if ((ret != FI_SUCCESS) && (ret != -FI_ENOSYS)) {
		printf("fi_eq_open %s\n", fi_strerror(-ret));
		exit(EXIT_FAILURE);
	}
	if (ret == -FI_ENOSYS) {
		async_avail = 0;
	}

	if (fi->domain_attr->av_type == FI_AV_UNSPEC) {
		test_map_type = test_table_type = 1;
	}

	if (fi->domain_attr->av_type == FI_AV_MAP) {
		test_map_type = 1;
	}

	if (fi->domain_attr->av_type == FI_AV_TABLE) {
		test_table_type = 1;
	}

	if ((test_map_type == 0) && (test_table_type == 0)) {
		printf("no map type available\n");
		ret = -1;
		goto err;
	}

	/*
	 * create an EP
	 */

	ret = fi_endpoint(domain, fi, &ep, NULL);
	if (ret != 0) {
		printf("fi_endpoint %s\n", fi_strerror(-ret));
		ret = -1;
		goto err;
	}

	if (test_map_type) {
	        ret = av_test_sync(FI_AV_MAP, num_entries, 0);
		if (ret != FI_SUCCESS)
			goto err;
	}

	if (test_table_type) {
	        ret = av_test_sync(FI_AV_TABLE, num_entries, 0);
		if (ret != FI_SUCCESS)
			goto err;
	}

	ret = fi_close(&ep->fid);
	if (ret != FI_SUCCESS) {
		printf("Error %d closing ep %s\n", ret,
			fi_strerror(-ret));
		goto err;
	}

	if (eq != NULL) {
		ret = fi_close(&eq->fid);
		if (ret != FI_SUCCESS) {
			printf("Error %d closing eq %s\n", ret,
				fi_strerror(-ret));
			goto err;
		}
	}

	ret = fi_close(&domain->fid);
	if (ret != FI_SUCCESS) {
		printf("Error %d closing domain %s\n", ret,
			fi_strerror(-ret));
		goto err;
	}

	ret = fi_close(&fabric->fid);
	if (ret != FI_SUCCESS) {
		printf("Error %d closing fabric: %s\n", ret, fi_strerror(-ret));
		exit(EXIT_FAILURE);
	}

	return ret;
err:
	if (domain != NULL) {
		ret = fi_close(&domain->fid);
		if (ret != FI_SUCCESS) {
			printf("Error in cleanup %d closing domain: %s\n",
			       ret, fi_strerror(-ret));
		}
		free(domain);
		domain = NULL;
	}

	if (fabric != NULL) {
		ret = fi_close(&fabric->fid);
		if (ret != FI_SUCCESS) {
			printf("Error in cleanup %d closing fabric: %s\n", ret,
			       fi_strerror(-ret));
		}
	}
	exit(EXIT_FAILURE);
}
