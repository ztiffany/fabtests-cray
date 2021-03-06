/*
 * Copyright (c) 2013-2014 Intel Corporation.  All rights reserved.
 *
 * This software is available to you under the BSD license
 * below:
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
#include <getopt.h>
#include <time.h>
#include <netdb.h>
#include <unistd.h>

#include <rdma/fabric.h>
#include <rdma/fi_errno.h>
#include <rdma/fi_endpoint.h>
#include <rdma/fi_cm.h>
#include <shared.h>
#include <math.h>

#define FT_CLOSEV(FID_C, NUM)			\
	do {					\
		int i;				\
		for (i = 0; i < NUM; i++) {	\
			if (FID_C[i])		\
				fi_close(&FID_C[i]->fid); \
		}				\
	} while (0)

static void *buf;
static size_t buffer_size = 1024;
static size_t transfer_size = 1000;
static int rx_depth = 512;

static struct fi_info *fi, *hints;
static char *dst_addr, *src_addr;
static char *src_port = "5100", *dst_port = "5100";

static int ctx_cnt = 2;
static int rx_ctx_bits = 0;
static struct fid_fabric *fab;
static struct fid_domain *dom;
static struct fid_ep *sep;
static struct fid_ep **tx_ep, **rx_ep;
static struct fid_cq **scq;
static struct fid_cq **rcq;
static struct fid_mr *mr;
static struct fid_av *av;
static void *local_addr, *remote_addr;
static size_t addrlen = 0;
static fi_addr_t remote_fi_addr;
struct fi_context fi_ctx_send;
struct fi_context fi_ctx_recv;
struct fi_context fi_ctx_av;
static fi_addr_t *remote_rx_addr;

void print_usage(char *name, char *desc)
{
	fprintf(stderr, "Usage:\n");
	fprintf(stderr, "  %s [OPTIONS]\t\tstart server\n", name);
	fprintf(stderr, "  %s [OPTIONS] <host>\tconnect to server \t\n", name);

	if (desc)
		fprintf(stderr, "\n%s\n", desc);

	fprintf(stderr, "\nOptions:\n");
	fprintf(stderr, "  -n <domain>\tdomain name\n");
	fprintf(stderr, "  -b <src_port>\tnon default source port number\n");
	fprintf(stderr, "  -p <dst_port>\tnon default destination port number\n");
	fprintf(stderr, "  -f <provider>\tspecific provider name eg IP, verbs\n");
	fprintf(stderr, "  -s <address>\tsource address\n");
	fprintf(stderr, "  -h\t\tdisplay this help output\n");

	return;
}

static int send_msg(int size)
{
	int ret;

	ret = fi_send(tx_ep[0], buf, (size_t) size, fi_mr_desc(mr),
			remote_rx_addr[0], &fi_ctx_send);
	if (ret) {
		FT_PRINTERR("fi_send", ret);
		return ret;
	}

	ret = wait_for_completion(scq[0], 1);

	return ret;
}

static int recv_msg(void)
{
	int ret;

	/* Messages sent to scalable EP fi_addr are received in context 0 */
	ret = fi_recv(rx_ep[0], buf, buffer_size, fi_mr_desc(mr), 0, &fi_ctx_recv);
	if (ret) {
		FT_PRINTERR("fi_recv", ret);
		return ret;
	}

	ret = wait_for_completion(rcq[0], 1);

	return ret;
}

static void free_ep_res(void)
{
	fi_close(&av->fid);
	fi_close(&mr->fid);
	FT_CLOSEV(rx_ep, ctx_cnt);
	FT_CLOSEV(tx_ep, ctx_cnt);
	FT_CLOSEV(rcq, ctx_cnt);
	FT_CLOSEV(scq, ctx_cnt);
	free(buf);
	free(rcq);
	free(scq);
	free(tx_ep);
	free(rx_ep);
}

static int alloc_ep_res(struct fid_ep *sep)
{
	struct fi_cq_attr cq_attr;
	struct fi_rx_attr rx_attr;
	struct fi_tx_attr tx_attr;
	struct fi_av_attr av_attr;
	int i, ret;

	buffer_size = test_size[TEST_CNT - 1].size;
	buf = malloc(buffer_size);

	scq = calloc(ctx_cnt, sizeof *scq);
	rcq = calloc(ctx_cnt, sizeof *rcq);

	tx_ep = calloc(ctx_cnt, sizeof *tx_ep);
	rx_ep = calloc(ctx_cnt, sizeof *rx_ep);

	remote_rx_addr = calloc(ctx_cnt, sizeof *remote_rx_addr);
	if (!buf || !scq || !rcq || !tx_ep || !rx_ep || !remote_rx_addr) {
		perror("malloc");
		return -1;
	}

	memset(&cq_attr, 0, sizeof cq_attr);
	cq_attr.format = FI_CQ_FORMAT_CONTEXT;
	cq_attr.wait_obj = FI_WAIT_NONE;
	cq_attr.size = rx_depth;
	
	for (i = 0; i < ctx_cnt; i++) {
		/* Create TX contexts: tx_ep */
		ret = fi_tx_context(sep, i, &tx_attr, &tx_ep[i], NULL);
		if (ret) {
			FT_PRINTERR("fi_tx_context", ret);
			goto err1;
		}

		ret = fi_cq_open(dom, &cq_attr, &scq[i], NULL);
		if (ret) {
			FT_PRINTERR("fi_cq_open", ret);
			goto err2;
		}
	}
	
	for (i = 0; i < ctx_cnt; i++) {
		/* Create RX contexts: rx_ep */
		ret = fi_rx_context(sep, i, &rx_attr, &rx_ep[i], NULL);
		if (ret) {
			FT_PRINTERR("fi_tx_context", ret);
			goto err3;
		}

		ret = fi_cq_open(dom, &cq_attr, &rcq[i], NULL);
		if (ret) {
			FT_PRINTERR("fi_cq_open", ret);
			goto err4;
		}
	}

	ret = fi_mr_reg(dom, buf, buffer_size, 0, 0, 0, 0, &mr, NULL);
	if (ret) {
		FT_PRINTERR("fi_mr_reg", ret);
		goto err5;
	}

	/* Get number of bits needed to represent ctx_cnt */
	while (ctx_cnt >> ++rx_ctx_bits);

	memset(&av_attr, 0, sizeof av_attr);
	av_attr.type = fi->domain_attr->av_type ?
			fi->domain_attr->av_type : FI_AV_MAP;
	av_attr.count = 1;
	av_attr.rx_ctx_bits = rx_ctx_bits;

	/* Open Address Vector */
	ret = fi_av_open(dom, &av_attr, &av, NULL);
	if (ret) {
		FT_PRINTERR("fi_av_open", ret);
		goto err6;
	}

	return 0;

err6:
	fi_close(&mr->fid);
err5:
	FT_CLOSEV(rcq, ctx_cnt);
err4:
	FT_CLOSEV(rx_ep, ctx_cnt);
err3:
	FT_CLOSEV(scq, ctx_cnt);
err2:
	FT_CLOSEV(tx_ep, ctx_cnt);
err1:
	free(buf);
	free(rcq);
	free(scq);
	free(tx_ep);
	free(rx_ep);
	free(remote_rx_addr);
	return ret;
}

static int bind_ep_res(void)
{
	int i, ret;

	for (i = 0; i < ctx_cnt; i++) {
		ret = fi_ep_bind(tx_ep[i], &scq[i]->fid, FI_SEND);
		if (ret) {
			FT_PRINTERR("fi_ep_bind", ret);
			return ret;
		}

		ret = fi_enable(tx_ep[i]);
		if (ret) {
			FT_PRINTERR("fi_enable", ret);
			return ret;
		}
	}

	for (i = 0; i < ctx_cnt; i++) {
		ret = fi_ep_bind(rx_ep[i], &rcq[i]->fid, FI_RECV);
		if (ret) {
			FT_PRINTERR("fi_ep_bind", ret);
			return ret;
		}

		ret = fi_enable(rx_ep[i]);
		if (ret) {
			FT_PRINTERR("fi_enable", ret);
			return ret;
		}
	}

	/* Bind scalable EP with AV */
	ret = fi_scalable_ep_bind(sep, &av->fid, 0);
	if (ret) {
		FT_PRINTERR("fi_ep_bind", ret);
		return ret;
	}

	if (ret)
		return ret;

	return ret;
}

static int run_test()
{
	int ret, i;

	/* Post recvs */
	for (i = 0; i < ctx_cnt; i++) {
		fprintf(stdout, "Posting recv for ctx: %d\n", i);
		ret = fi_recv(rx_ep[i], buf, buffer_size, fi_mr_desc(mr), 0, NULL);
		if (ret) {
			FT_PRINTERR("fi_recv", ret);
			return ret;
		}
	}

	if (dst_addr) {
		/* Post sends directly to each of the recv contexts */
		for (i = 0; i < ctx_cnt; i++) {
			fprintf(stdout, "Posting send for ctx: %d\n", i);
			ret = fi_send(tx_ep[i], buf, transfer_size, fi_mr_desc(mr),
					remote_rx_addr[i], NULL); 
			if (ret) {
				FT_PRINTERR("fi_recv", ret);
				return ret;
			}

			wait_for_completion(scq[i], 1);
		}
	} else {
		for (i = 0; i < ctx_cnt; i++) {
			fprintf(stdout, "wait for recv completion for ctx: %d\n", i);
			wait_for_completion(rcq[i], 1);
		}
	}

	return 0;
}

static int init_fabric(void)
{
	uint64_t flags = 0;
	char *node, *service;
	int ret;

	if (dst_addr) {
		ret = ft_getsrcaddr(src_addr, src_port, hints);
		if (ret)
			return ret;
		node = dst_addr;
		service = dst_port;
	} else {
		node = src_addr;
		service = src_port;
		flags = FI_SOURCE;
	}

	ret = fi_getinfo(FT_FIVERSION, node, service, flags, hints, &fi);
	if (ret) {
		FT_PRINTERR("fi_getinfo", ret);
		return ret;
	}

	/* Check the optimal number of TX and RX contexts supported by the provider */
	ctx_cnt = MIN(ctx_cnt, fi->domain_attr->tx_ctx_cnt);
	ctx_cnt = MIN(ctx_cnt, fi->domain_attr->rx_ctx_cnt);
	if (!ctx_cnt) {
		fprintf(stderr, "Provider doesn't support contexts\n");
		return 1;
	}

	/* We use provider MR attributes and direct address (no offsets) for RMA calls */
	if (!(fi->mode & FI_PROV_MR_ATTR))
		fi->mode |= FI_PROV_MR_ATTR;

	/* Get remote address */
	if (dst_addr) {
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

	/* Set the required number of TX and RX context counts */
	fi->ep_attr->tx_ctx_cnt = ctx_cnt;
	fi->ep_attr->rx_ctx_cnt = ctx_cnt;

	ret = fi_scalable_ep(dom, fi, &sep, NULL);
	if (ret) {
		FT_PRINTERR("fi_scalable_ep", ret);
		goto err2;
	}

	ret = alloc_ep_res(sep);
	if (ret)
		goto err3;

	ret = bind_ep_res();
	if (ret)
		goto err4;

	return 0;

err4:
	free_ep_res();
err3:
	fi_close(&sep->fid);
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
	int i;

	if (dst_addr) {
		/* Get local address blob. Find the addrlen first. We set addrlen 
		 * as 0 and fi_getname will return the actual addrlen. */
		addrlen = 0;
		ret = fi_getname(&sep->fid, local_addr, &addrlen);
		if (ret != -FI_ETOOSMALL) {
			FT_PRINTERR("fi_getname", ret);
			return ret;
		}

		local_addr = malloc(addrlen);
		ret = fi_getname(&sep->fid, local_addr, &addrlen);
		if (ret) {
			FT_PRINTERR("fi_getname", ret);
			return ret;
		}

		ret = fi_av_insert(av, remote_addr, 1, &remote_fi_addr, 0, &fi_ctx_av);
		if (ret != 1) {
			FT_PRINTERR("fi_av_insert", ret);
			return ret;
		}

		for (i = 0; i < ctx_cnt; i++)
			remote_rx_addr[i] = fi_rx_addr(remote_fi_addr, i, rx_ctx_bits);

		/* Send local addr size and local addr */
		memcpy(buf, &addrlen, sizeof(size_t));
		memcpy(buf + sizeof(size_t), local_addr, addrlen);
		ret = send_msg(sizeof(size_t) + addrlen);
		if (ret)
			return ret;

		/* Receive ACK from server */
		ret = recv_msg();
		if (ret)
			return ret;

	} else {
		/* Post a recv to get the remote address */
		ret = recv_msg();
		if (ret)
			return ret;

		memcpy(&addrlen, buf, sizeof(size_t));
		remote_addr = malloc(addrlen);
		memcpy(remote_addr, buf + sizeof(size_t), addrlen);

		ret = fi_av_insert(av, remote_addr, 1, &remote_fi_addr, 0, &fi_ctx_av);
		if (ret != 1) {
			FT_PRINTERR("fi_av_insert", ret);
			return ret;
		}

		/* Send ACK */
		ret = send_msg(16);
		if (ret)
			return ret;
	}

	return 0;
}

static int run(void)
{
	int ret = 0;

	ret = init_fabric();
	if (ret)
		return ret;

	ret = init_av();
	if (ret)
		goto out;

	run_test();
	/*TODO: Add a local finalize applicable for scalable ep */
	//ft_finalize(tx_ep[0], scq[0], rcq[0], remote_rx_addr[0]);
out:
	free_ep_res();
	fi_close(&sep->fid);
	fi_close(&dom->fid);
	fi_close(&fab->fid);
	return ret;
}

int main(int argc, char **argv)
{
	int ret, op;

	hints = fi_allocinfo();
	if (!hints)
		return EXIT_FAILURE;

	while ((op = getopt(argc, argv, "b:p:s:h" INFO_OPTS)) != -1) {
		switch (op) {
		case 'b':
			src_port = optarg;
			break;
		case 'p':
			dst_port = optarg;
			break;
		case 's':
			src_addr = optarg;
			break;
		default:
			ft_parseinfo(op, optarg, hints);
			break;
		case '?':
		case 'h':
			print_usage(argv[0], "An RDM client-server example with scalable endpoints.\n");
			return EXIT_FAILURE;
		}
	}

	if (optind < argc)
		dst_addr = argv[optind];

	hints->ep_attr->type = FI_EP_RDM;
	hints->caps = FI_MSG | FI_NAMED_RX_CTX;
	hints->mode = FI_CONTEXT | FI_LOCAL_MR | FI_PROV_MR_ATTR;
	hints->addr_format = FI_SOCKADDR;

	ret = run();
	fi_freeinfo(hints);
	fi_freeinfo(fi);
	return ret;
}
