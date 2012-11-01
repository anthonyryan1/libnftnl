/*
 * (C) 2012 by Pablo Neira Ayuso <pablo@netfilter.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This code has been sponsored by Sophos Astaro <http://www.sophos.com>
 */

#include "internal.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <arpa/inet.h>

#include <libmnl/libmnl.h>
#include <linux/netfilter/nf_tables.h>
#include <libnftables/expr.h>
#include "expr_ops.h"
#include "data_reg.h"

struct nft_expr_cmp {
	union nft_data_reg	data;
	uint8_t			sreg;	/* enum nft_registers */
	uint8_t			op;	/* enum nft_cmp_ops */
};

static int
nft_rule_expr_cmp_set(struct nft_rule_expr *e, uint16_t type,
		      const void *data, size_t data_len)
{
	struct nft_expr_cmp *cmp = (struct nft_expr_cmp *)e->data;

	switch(type) {
	case NFT_EXPR_CMP_SREG:
		cmp->sreg = *((uint32_t *)data);
		break;
	case NFT_EXPR_CMP_OP:
		cmp->op = *((uint32_t *)data);
		break;
	case NFT_EXPR_CMP_DATA:
		memcpy(&cmp->data.val, data, data_len);
		cmp->data.len = data_len;
		break;
	default:
		return -1;
	}
	return 0;
}

static const void *
nft_rule_expr_cmp_get(struct nft_rule_expr *e, uint16_t type, size_t *data_len)
{
	struct nft_expr_cmp *cmp = (struct nft_expr_cmp *)e->data;

	switch(type) {
	case NFT_EXPR_CMP_SREG:
		if (e->flags & (1 << NFT_EXPR_CMP_SREG)) {
			*data_len = sizeof(cmp->sreg);
			return &cmp->sreg;
		} else
			return NULL;
		break;
	case NFT_EXPR_CMP_OP:
		if (e->flags & (1 << NFT_EXPR_CMP_OP)) {
			*data_len = sizeof(cmp->op);
			return &cmp->op;
		} else
			return NULL;
		break;
	case NFT_EXPR_CMP_DATA:
		if (e->flags & (1 << NFT_EXPR_CMP_DATA)) {
			*data_len = cmp->data.len;
			return &cmp->data.val;
		} else
			return NULL;
		break;
	default:
		break;
	}
	return NULL;
}

static int nft_rule_expr_cmp_cb(const struct nlattr *attr, void *data)
{
	const struct nlattr **tb = data;
	int type = mnl_attr_get_type(attr);

	if (mnl_attr_type_valid(attr, NFTA_CMP_MAX) < 0)
		return MNL_CB_OK;

	switch(type) {
	case NFTA_CMP_SREG:
	case NFTA_CMP_OP:
		if (mnl_attr_validate(attr, MNL_TYPE_U32) < 0) {
			perror("mnl_attr_validate");
			return MNL_CB_ERROR;
		}
		break;
	case NFTA_CMP_DATA:
		if (mnl_attr_validate(attr, MNL_TYPE_BINARY) < 0) {
			perror("mnl_attr_validate");
			return MNL_CB_ERROR;
		}
		break;
	}

	tb[type] = attr;
	return MNL_CB_OK;
}

static void
nft_rule_expr_cmp_build(struct nlmsghdr *nlh, struct nft_rule_expr *e)
{
	struct nft_expr_cmp *cmp = (struct nft_expr_cmp *)e->data;

	if (e->flags & (1 << NFT_EXPR_CMP_SREG))
		mnl_attr_put_u32(nlh, NFTA_CMP_SREG, htonl(cmp->sreg));
	if (e->flags & (1 << NFT_EXPR_CMP_OP))
		mnl_attr_put_u32(nlh, NFTA_CMP_OP, htonl(cmp->op));
	if (e->flags & (1 << NFT_EXPR_CMP_DATA)) {
		struct nlattr *nest;

		nest = mnl_attr_nest_start(nlh, NFTA_CMP_DATA);
		mnl_attr_put(nlh, NFTA_DATA_VALUE, cmp->data.len, cmp->data.val);
		mnl_attr_nest_end(nlh, nest);
	}
}

static int
nft_rule_expr_cmp_parse(struct nft_rule_expr *e, struct nlattr *attr)
{
	struct nft_expr_cmp *cmp = (struct nft_expr_cmp *)e->data;
	struct nlattr *tb[NFTA_CMP_MAX+1] = {};
	int ret = 0;

	if (mnl_attr_parse_nested(attr, nft_rule_expr_cmp_cb, tb) < 0)
		return -1;

	if (tb[NFTA_CMP_SREG]) {
		cmp->sreg = ntohl(mnl_attr_get_u32(tb[NFTA_CMP_SREG]));
		e->flags |= (1 << NFTA_CMP_SREG);
	}
	if (tb[NFTA_CMP_OP]) {
		cmp->op = ntohl(mnl_attr_get_u32(tb[NFTA_CMP_OP]));
		e->flags |= (1 << NFTA_CMP_OP);
	}
	if (tb[NFTA_CMP_DATA]) {
		ret = nft_parse_data(&cmp->data, tb[NFTA_CMP_DATA], NULL);
		e->flags |= (1 << NFTA_CMP_DATA);
	}

	return ret;
}

static char *expr_cmp_str[] = {
	[NFT_CMP_EQ]	= "eq",
	[NFT_CMP_NEQ]	= "neq",
	[NFT_CMP_LT]	= "lt",
	[NFT_CMP_LTE]	= "lte",
	[NFT_CMP_GT]	= "gt",
	[NFT_CMP_GTE]	= "gte",
};

static int
nft_rule_expr_cmp_snprintf(char *buf, size_t size, struct nft_rule_expr *e)
{
	struct nft_expr_cmp *cmp = (struct nft_expr_cmp *)e->data;
	int len = size, offset = 0, ret, i;

	ret = snprintf(buf, len, "sreg=%u op=%s data=",
			cmp->sreg, expr_cmp_str[cmp->op]);
	SNPRINTF_BUFFER_SIZE(ret, size, len, offset);

	for (i=0; i<cmp->data.len/sizeof(uint32_t); i++) {
		ret = snprintf(buf+offset, len, "%.8x ", cmp->data.val[i]);
		SNPRINTF_BUFFER_SIZE(ret, size, len, offset);
	}
	return offset;
}

struct expr_ops expr_ops_cmp = {
	.name		= "cmp",
	.alloc_len	= sizeof(struct nft_expr_cmp),
	.max_attr	= NFTA_CMP_MAX,
	.set		= nft_rule_expr_cmp_set,
	.get		= nft_rule_expr_cmp_get,
	.parse		= nft_rule_expr_cmp_parse,
	.build		= nft_rule_expr_cmp_build,
	.snprintf	= nft_rule_expr_cmp_snprintf,
};