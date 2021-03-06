/*
 * Copyright (c) 2013-2014 Intel Corporation.  All rights reserved.
 *
 * This software is available to you under the BSD license below:
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
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <getopt.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <netdb.h>
#include <fcntl.h>
#include <unistd.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <time.h>

#include <rdma/fabric.h>
#include <rdma/fi_endpoint.h>
#include <rdma/fi_domain.h>
#include <rdma/fi_tagged.h>
#include <rdma/fi_rma.h>
#include <rdma/fi_cm.h>
#include <rdma/fi_errno.h>
#include <rdma/fi_atomic.h>

#include "shared.h"

struct addr_key {
	uint64_t addr;
	uint64_t key;
};

static struct cs_opts opts;
static enum fi_op op_type = FI_MIN;
static char test_name[10] = "custom";
static struct timespec start, end;
static void *buf;
static void *result;
static void *compare;
static size_t buffer_size;
struct addr_key local, remote;

static struct fi_info *fi, *hints;

static struct fid_fabric *fab;
static struct fid_domain *dom;
static struct fid_ep *ep;
static struct fid_av *av;
static struct fid_cq *rcq, *scq;
static struct fid_mr *mr;
static struct fid_mr *mr_result;
static struct fid_mr *mr_compare;
static void *local_addr, *remote_addr;
static size_t addrlen = 0;
static fi_addr_t remote_fi_addr;
static struct fi_context fi_ctx_send;
static struct fi_context fi_ctx_recv;
static struct fi_context fi_ctx_atomic;
static struct fi_context fi_ctx_av;

// performing atmics operation on UINT_64 as an example
static enum fi_datatype datatype = FI_UINT64;
static size_t *count;
static int run_all_ops = 1;

static const char* get_fi_op_name(enum fi_op op)
{
	switch (op) {
	case FI_MIN: return "min";
	case FI_MAX: return "max";
	case FI_ATOMIC_READ: return "read";
	case FI_ATOMIC_WRITE: return "write";
	case FI_CSWAP: return "cswap";
	default: return "";
	}	
}

static enum fi_op get_fi_op(char *op) {
	if (!strcmp(op, "min"))
		return FI_MIN;
	else if (!strcmp(op, "max"))
		return FI_MAX;
	else if (!strcmp(op, "read"))
		return FI_ATOMIC_READ;
	else if (!strcmp(op, "write"))
		return FI_ATOMIC_WRITE;
	else if (!strcmp(op, "cswap"))
		return FI_CSWAP;
	else {
		fprintf(stderr, "Not supported by the example\n");
		return FI_ATOMIC_OP_LAST;
	}
}

static int post_recv(void)
{
	int ret;
	
	ret = fi_recv(ep, buf, buffer_size, fi_mr_desc(mr), 0, &fi_ctx_recv);
	if (ret){
		FT_PRINTERR("fi_recv", ret);
		return ret;
	}
	
	return wait_for_completion(rcq, 1);
}

static int send_msg(int size)
{
	int ret;
	
	ret = fi_send(ep, buf, (size_t) size, fi_mr_desc(mr), remote_fi_addr, 
			&fi_ctx_send);
	if (ret)
		FT_PRINTERR("fi_send", ret);

	return wait_for_completion(scq, 1);
}

static int sync_test(void)
{
	int ret;

	ret = opts.dst_addr ? send_msg(16) : post_recv();
	if (ret)
		return ret;

	return opts.dst_addr ? post_recv() : send_msg(16);
}

static int is_valid_base_atomic_op(enum fi_op op)
{	
	int ret;

	ret = fi_atomicvalid(ep, datatype, op, count);
	if (ret) {
		fprintf(stderr, "Provider doesn't support %s"
				" base atomic operation\n", get_fi_op_name(op));
		return 0;
	}
	
	return 1;		
}

static int is_valid_fetch_atomic_op(enum fi_op op)
{		
	int ret;

	ret = fi_fetch_atomicvalid(ep, datatype, op, count);
	if (ret) {
		fprintf(stderr, "Provider doesn't support %s"
				" fetch atomic operation\n", get_fi_op_name(op));
		return 0;
	}
	
	return 1;		
}

static int is_valid_compare_atomic_op(enum fi_op op)
{		
	int ret;

	ret = fi_compare_atomicvalid(ep, datatype, op, count);
	if (ret) {
		fprintf(stderr, "Provider doesn't support %s"
				" compare atomic operation\n", get_fi_op_name(op));
		return 0;
	}
	
	return 1;		
}


static int execute_base_atomic_op(enum fi_op op)
{
	int ret;
	
	ret = fi_atomic(ep, buf, 1, fi_mr_desc(mr), remote_fi_addr, remote.addr,
		       	remote.key, datatype, op, &fi_ctx_atomic);
	if (ret) {
		FT_PRINTERR("fi_atomic", ret);
	} else {						
		ret = wait_for_completion(scq, 1);
	}
	
	return ret;
}

static int execute_fetch_atomic_op(enum fi_op op)
{
	int ret;
		
	ret = fi_fetch_atomic(ep, buf, 1, fi_mr_desc(mr), result, 
			fi_mr_desc(mr_result), remote_fi_addr, remote.addr, 
			remote.key, datatype, op, &fi_ctx_atomic);
	if (ret) {
		FT_PRINTERR("fi_fetch_atomic", ret);
	} else {						
		ret = wait_for_completion(scq, 1);
	}
	
	return ret;
}

static int execute_compare_atomic_op(enum fi_op op)
{
	int ret;
	
	ret = fi_compare_atomic(ep, buf, 1, fi_mr_desc(mr), compare, 
			fi_mr_desc(mr_compare), result, fi_mr_desc(mr_result), 
			remote_fi_addr, remote.addr, remote.key, datatype, op, 
			&fi_ctx_atomic);
	if (ret) {
		FT_PRINTERR("fi_compare_atomic", ret);
	} else {			
		ret = wait_for_completion(scq, 1);
	}
	
	return ret;
}

static int run_op(void)
{
	int ret, i;

	count = (size_t *) malloc(sizeof(size_t));
	sync_test();
	clock_gettime(CLOCK_MONOTONIC, &start);
	
	switch (op_type) {
	case FI_MIN:
	case FI_MAX:
	case FI_ATOMIC_READ:
	case FI_ATOMIC_WRITE:
		ret = is_valid_base_atomic_op(op_type);
		if (ret > 0) {
			for (i = 0; i < opts.iterations; i++) {
				ret = execute_base_atomic_op(op_type);
				if (ret)
					break;
			}
		}

		ret = is_valid_fetch_atomic_op(op_type);
		if (ret > 0) {
			for (i = 0; i < opts.iterations; i++) {
				ret = execute_fetch_atomic_op(op_type);
				if (ret)
					break;
			}
		}
		break;
	case FI_CSWAP:
		ret = is_valid_compare_atomic_op(op_type);
		if (ret > 0) {
			for (i = 0; i < opts.iterations; i++) {
				ret = execute_compare_atomic_op(op_type);
				if(ret)
					break;
			}	
		}
		break;
	default:
		ret = -EINVAL;
		goto out;
	}

	clock_gettime(CLOCK_MONOTONIC, &end);
	
	if (ret)
		goto out;

	if (opts.machr)
		show_perf_mr(opts.transfer_size, opts.iterations, &start, &end,
				op_type == FI_CSWAP ? 1 : 2, opts.argc, opts.argv);
	else
		show_perf(test_name, opts.transfer_size, opts.iterations, 
				&start, &end, op_type == FI_CSWAP ? 1 : 2);

	ret = 0;
out:
	free(count);
	return ret;
}

static int run_ops(void)
{
	int ret;

	op_type = FI_MIN;
	ret = run_op();
	if (ret)
		return ret;

	op_type = FI_MAX;
	ret = run_op();
	if (ret)
		return ret;

	op_type = FI_ATOMIC_READ;
	ret = run_op();
	if (ret)
		return ret;

	op_type = FI_ATOMIC_WRITE;
	ret = run_op();
	if (ret)
		return ret;

	op_type = FI_CSWAP;
	ret = run_op();
	if (ret)
		return ret;

	return 0;
}

static int run_test(void)
{
	return run_all_ops ? run_ops() : run_op();
}

static void free_ep_res(void)
{
	fi_close(&av->fid);
	fi_close(&mr->fid);
	fi_close(&mr_result->fid);
	fi_close(&mr_compare->fid);
	fi_close(&rcq->fid);
	fi_close(&scq->fid);
	free(buf);
	free(result);
	free(compare);
}

static int alloc_ep_res(struct fi_info *fi)
{
	struct fi_cq_attr cq_attr;
	struct fi_av_attr av_attr;
	int ret;

	buffer_size = opts.user_options & FT_OPT_SIZE ?
			opts.transfer_size : test_size[TEST_CNT - 1].size;
	buf = malloc(MAX(buffer_size, sizeof(uint64_t)));
	if (!buf) {
		perror("malloc");
		return -1;
	}

	result = malloc(MAX(buffer_size, sizeof(uint64_t)));
	if (!result) {
		perror("malloc");
		return -1;
	}
	
	compare = malloc(MAX(buffer_size, sizeof(uint64_t)));
	if (!compare) {
		perror("malloc");
		return -1;
	}
	
	memset(&cq_attr, 0, sizeof cq_attr);
	cq_attr.format = FI_CQ_FORMAT_CONTEXT;
	cq_attr.wait_obj = FI_WAIT_NONE;
	cq_attr.size = 128;
	ret = fi_cq_open(dom, &cq_attr, &scq, NULL);
	if (ret) {
		FT_PRINTERR("fi_cq_open", ret);
		goto err1;
	}

	ret = fi_cq_open(dom, &cq_attr, &rcq, NULL);
	if (ret) {
		FT_PRINTERR("fi_cq_open", ret);
		goto err2;
	}
	
	// registers local data buffer buff that specifies 
	// the first operand of the atomic operation
	ret = fi_mr_reg(dom, buf, MAX(buffer_size, sizeof(uint64_t)), 
		FI_REMOTE_READ | FI_REMOTE_WRITE, 0, 0, 0, &mr, NULL);
	if (ret) {
		FT_PRINTERR("fi_mr_reg", ret);
		goto err3;
	}

	// registers local data buffer that stores initial value of 
	// the remote buffer
	ret = fi_mr_reg(dom, result, MAX(buffer_size, sizeof(uint64_t)), 
		FI_REMOTE_READ | FI_REMOTE_WRITE, 0, 0, 0, &mr_result, NULL);
	if (ret) {
		FT_PRINTERR("fi_mr_reg", -ret);
		goto err4;
	}
	
	// registers local data buffer that contains comparison data
	ret = fi_mr_reg(dom, compare, MAX(buffer_size, sizeof(uint64_t)), 
		FI_REMOTE_READ | FI_REMOTE_WRITE, 0, 0, 0, &mr_compare, NULL);
	if (ret) {
		FT_PRINTERR("fi_mr_reg", ret);
		goto err5;
	}

	memset(&av_attr, 0, sizeof av_attr);
	av_attr.type = fi->domain_attr->av_type ?
			fi->domain_attr->av_type : FI_AV_MAP;
	av_attr.count = 1;
	av_attr.name = NULL;

	ret = fi_av_open(dom, &av_attr, &av, NULL);
	if (ret) {
		FT_PRINTERR("fi_av_open", ret);
		goto err6;
	}
	
	return 0;

err6:
	fi_close(&mr_compare->fid);
err5:
	fi_close(&mr_result->fid);
err4:
	fi_close(&mr->fid);
err3:
	fi_close(&rcq->fid);
err2:
	fi_close(&scq->fid);
err1:
	free(buf);
	free(result);
	free(compare);
	
	return ret;
}

static int bind_ep_res(void)
{
	int ret;

	ret = fi_ep_bind(ep, &scq->fid, FI_SEND | FI_READ | FI_WRITE);
	if (ret) {
		FT_PRINTERR("fi_ep_bind", -ret);
		return ret;
	}

	ret = fi_ep_bind(ep, &rcq->fid, FI_RECV);
	if (ret) {
		FT_PRINTERR("fi_ep_bind", -ret);
		return ret;
	}

	ret = fi_ep_bind(ep, &av->fid, FI_RECV);
	if (ret) {
		FT_PRINTERR("fi_ep_bind", ret);
		return ret;
	}

	ret = fi_enable(ep);
	if(ret) {
		FT_PRINTERR("fi_enable", ret);
		return ret;
	}
	
	/* Post the first recv buffer */
	ret = fi_recv(ep, buf, buffer_size, fi_mr_desc(mr), 0, &fi_ctx_recv);
	if (ret) {
		FT_PRINTERR("fi_recv", ret);
		return ret;
	}

	return ret;
}

static int init_fabric(void)
{
	uint64_t flags = 0;
	char *node, *service;
	int ret;

	if (opts.dst_addr) {
		ret = ft_getsrcaddr(opts.src_addr, opts.src_port, hints);
		if (ret)
			return ret;
		node = opts.dst_addr;
		service = opts.dst_port;
	} else {
		node = opts.src_addr;
		service = opts.src_port;
		flags = FI_SOURCE;
	}

	ret = fi_getinfo(FT_FIVERSION, node, service, flags, hints, &fi);
	if (ret) {
		FT_PRINTERR("fi_getinfo", ret);
		return ret;
	}

	// we use provider MR attributes and direct address (no offsets) 
	// for RMA calls
	if (!(fi->mode & FI_PROV_MR_ATTR))
		fi->mode |= FI_PROV_MR_ATTR;

	// get remote address
	if (opts.dst_addr) {
		addrlen = fi->dest_addrlen;
		remote_addr = malloc(addrlen);
		memcpy(remote_addr, fi->dest_addr, addrlen);
	}

	ret = fi_fabric(fi->fabric_attr, &fab, NULL);
	if (ret) {
		FT_PRINTERR("fi_fabric", ret);
		goto err0;
	}

	ret = fi_domain(fab, fi, &dom, NULL);
	if (ret) {
		FT_PRINTERR("fi_domain", ret);
		goto err1;
	}

	ret = fi_endpoint(dom, fi, &ep, NULL);
	if (ret) {
		FT_PRINTERR("fi_endpoint", ret);
		goto err2;
	}

	ret = alloc_ep_res(fi);
	if (ret)
		goto err3;
	
	ret = bind_ep_res();
	if (ret)
		goto err4;
	
	return 0;

err4:
	free_ep_res();
err3:
	fi_close(&ep->fid);
err2:
	fi_close(&dom->fid);
err1:
	fi_close(&fab->fid);
err0:
	return ret;
}

static int init_av(void)
{
	int ret;

	if (opts.dst_addr) {
		// get local address blob. Find the addrlen first. We set 
		// addrlen as 0 and fi_getname will return the actual addrlen
		addrlen = 0;
		ret = fi_getname(&ep->fid, local_addr, &addrlen);
		if (ret != -FI_ETOOSMALL) {
			FT_PRINTERR("fi_getname", ret);
			return ret;
		}

		local_addr = malloc(addrlen);
		ret = fi_getname(&ep->fid, local_addr, &addrlen);
		if (ret) {
			FT_PRINTERR("fi_getname", ret);
			return ret;
		}

		ret = fi_av_insert(av, remote_addr, 1, &remote_fi_addr, 0, 
				&fi_ctx_av);
		if (ret != 1) {
			FT_PRINTERR("fi_av_insert", ret);
			return ret;
		}

		// send local addr size and local addr
		memcpy(buf, &addrlen, sizeof(size_t));
		memcpy(buf + sizeof(size_t), local_addr, addrlen);
		ret = send_msg(sizeof(size_t) + addrlen);
		if (ret)
			return ret;
	
		// receive ACK from server
		ret = post_recv();
		if (ret)
			return ret;
	} else {
		// post a recv to get the remote address
		ret = post_recv();
		if (ret)
			return ret;

		memcpy(&addrlen, buf, sizeof(size_t));
		remote_addr = malloc(addrlen);
		memcpy(remote_addr, buf + sizeof(size_t), addrlen);

		ret = fi_av_insert(av, remote_addr, 1, &remote_fi_addr, 0, 
				&fi_ctx_av);
		if (ret != 1) {
			FT_PRINTERR("fi_av_insert", ret);
			return ret;
		}

		// send ACK 
		ret = send_msg(16);
		if (ret)
			return ret;
	}

	return ret;	
}

static int exchange_addr_key(void)
{
	int ret;
	int len = sizeof(local);

	local.addr = (uintptr_t) buf;
	local.key = fi_mr_key(mr);

	if (opts.dst_addr) {
		*(struct addr_key *)buf = local;
		ret = send_msg(len);
		if(ret)
			return ret;

		ret = post_recv();
		if(ret)
			return ret;
		remote = *(struct addr_key *)buf;
	} else {
		ret = post_recv();
		if(ret)
			return ret;
		remote = *(struct addr_key *)buf;
		
		*(struct addr_key *)buf = local;
		ret = send_msg(len);
		if(ret)
			return ret;
	}

	return 0;
}

static int run(void)
{
	int i, ret = 0;
	
	ret = init_fabric();
	if (ret)
			return ret;

	ret = init_av();
	if (ret)
			goto out;

	ret = exchange_addr_key();
	if (ret)
		goto out;

	if (!(opts.user_options & FT_OPT_SIZE)) {
		for (i = 0; i < TEST_CNT; i++) {
			if (test_size[i].option > opts.size_option)
				continue;
			opts.transfer_size = test_size[i].size;
			init_test(&opts, test_name, sizeof(test_name));
			ret = run_test();
			if (ret)
				goto out;
		}
	} else {
		init_test(&opts, test_name, sizeof(test_name));
		ret = run_test();
		if (ret)
			goto out;
	}
	/* Finalize before closing ep */
	ft_finalize(ep, scq, rcq, remote_fi_addr);
out:
	fi_close(&ep->fid);
	free_ep_res();
	fi_close(&dom->fid);
	fi_close(&fab->fid);
	
	return ret;
}

int main(int argc, char **argv)
{
	int op, ret;
	opts = INIT_OPTS;

	hints = fi_allocinfo();
	if (!hints)
		return EXIT_FAILURE;

	while ((op = getopt(argc, argv, "ho:" CS_OPTS INFO_OPTS)) != -1) {
		switch (op) {
		case 'o':
			if (!strncasecmp("all", optarg, 3)) {
				run_all_ops = 1;
			} else {
				run_all_ops = 0;
				op_type = get_fi_op(optarg);
				if (op_type == FI_ATOMIC_OP_LAST) { 
					ft_csusage(argv[0], NULL);
					fprintf(stderr, "  -o <op>\tatomic op type: all|min|max|read|write|cswap (default: all)]\n");
					return EXIT_FAILURE;
				}
			}
			break;
		default:
			ft_parseinfo(op, optarg, hints);
			ft_parsecsopts(op, optarg, &opts);
			break;
		case '?':
		case 'h':
			ft_csusage(argv[0], "Ping pong client and server using atomic ops.");
			fprintf(stderr, "  -o <op>\tatomic op type: all|min|max|read|write|cswap (default: all)]\n");
			return EXIT_FAILURE;
		}
	}

	if (optind < argc)
		opts.dst_addr = argv[optind];

	hints->ep_attr->type = FI_EP_RDM;
	hints->caps = FI_MSG | FI_ATOMICS;
	hints->mode = FI_CONTEXT | FI_LOCAL_MR | FI_PROV_MR_ATTR;

	if (opts.prhints) {
		printf("%s", fi_tostr(&hints, FI_TYPE_INFO));
		ret = EXIT_SUCCESS;
	} else {
		ret = run();
	}
	fi_freeinfo(hints);
	fi_freeinfo(fi);
	return ret;
}
