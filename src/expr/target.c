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
#include <string.h>	/* for memcpy */
#include <arpa/inet.h>

#include <libmnl/libmnl.h>

#include <linux/netfilter/nf_tables.h>
#include <linux/netfilter/x_tables.h>

#include <libnftables/expr.h>

#include "expr_ops.h"

struct nft_expr_target {
	char		name[XT_EXTENSION_MAXNAMELEN];
	uint32_t	rev;
	uint32_t	data_len;
	const void	*data;
};

static int
nft_rule_expr_target_set(struct nft_rule_expr *e, uint16_t type,
			 const void *data, size_t data_len)
{
	struct nft_expr_target *tg = (struct nft_expr_target *)e->data;

	switch(type) {
	case NFT_EXPR_TG_NAME:
		memcpy(tg->name, data, XT_EXTENSION_MAXNAMELEN);
		tg->name[XT_EXTENSION_MAXNAMELEN-1] = '\0';
		break;
	case NFT_EXPR_TG_REV:
		tg->rev = *((uint32_t *)data);
		break;
	case NFT_EXPR_TG_INFO:
		if (tg->data)
			free((void *)tg->data);

		tg->data = data;
		tg->data_len = data_len;
		break;
	default:
		return -1;
	}
	return 0;
}

static const void *
nft_rule_expr_target_get(struct nft_rule_expr *e, uint16_t type,
			 size_t *data_len)
{
	struct nft_expr_target *tg = (struct nft_expr_target *)e->data;

	switch(type) {
	case NFT_EXPR_TG_NAME:
		if (e->flags & (1 << NFT_EXPR_TG_NAME)) {
			*data_len = sizeof(tg->name);
			return tg->name;
		} else
			return NULL;
		break;
	case NFT_EXPR_TG_REV:
		if (e->flags & (1 << NFT_EXPR_TG_REV)) {
			*data_len = sizeof(tg->rev);
			return &tg->rev;
		} else
			return NULL;
		break;
	case NFT_EXPR_TG_INFO:
		if (e->flags & (1 << NFT_EXPR_TG_INFO)) {
			*data_len = tg->data_len;
			return tg->data;
		} else
			return NULL;
		break;
	default:
		break;
	}
	return NULL;
}

static int nft_rule_expr_target_cb(const struct nlattr *attr, void *data)
{
	const struct nlattr **tb = data;
	int type = mnl_attr_get_type(attr);

	if (mnl_attr_type_valid(attr, NFTA_TARGET_MAX) < 0)
		return MNL_CB_OK;

	switch(type) {
	case NFTA_TARGET_NAME:
		if (mnl_attr_validate(attr, MNL_TYPE_NUL_STRING) < 0) {
			perror("mnl_attr_validate");
			return MNL_CB_ERROR;
		}
		break;
	case NFTA_TARGET_REV:
		if (mnl_attr_validate(attr, MNL_TYPE_U32) < 0) {
			perror("mnl_attr_validate");
			return MNL_CB_ERROR;
		}
		break;
	case NFTA_TARGET_INFO:
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
nft_rule_expr_target_build(struct nlmsghdr *nlh, struct nft_rule_expr *e)
{
	struct nft_expr_target *tg = (struct nft_expr_target *)e->data;

	if (e->flags & (1 << NFT_EXPR_TG_NAME))
		mnl_attr_put_strz(nlh, NFTA_TARGET_NAME, tg->name);
	if (e->flags & (1 << NFT_EXPR_TG_REV))
		mnl_attr_put_u32(nlh, NFTA_TARGET_REV, htonl(tg->rev));
	if (e->flags & (1 << NFT_EXPR_TG_INFO))
		mnl_attr_put(nlh, NFTA_TARGET_INFO, XT_ALIGN(tg->data_len), tg->data);
}

static int nft_rule_expr_target_parse(struct nft_rule_expr *e, struct nlattr *attr)
{
	struct nft_expr_target *target = (struct nft_expr_target *)e->data;
	struct nlattr *tb[NFTA_TARGET_MAX+1] = {};

	if (mnl_attr_parse_nested(attr, nft_rule_expr_target_cb, tb) < 0)
		return -1;

	if (tb[NFTA_TARGET_NAME]) {
		snprintf(target->name, XT_EXTENSION_MAXNAMELEN, "%s",
			 mnl_attr_get_str(tb[NFTA_TARGET_NAME]));

		target->name[XT_EXTENSION_MAXNAMELEN-1] = '\0';
		e->flags |= (1 << NFT_EXPR_TG_NAME);
	}

	if (tb[NFTA_TARGET_REV]) {
		target->rev = ntohl(mnl_attr_get_u32(tb[NFTA_TARGET_REV]));
		e->flags |= (1 << NFT_EXPR_TG_REV);
	}

	if (tb[NFTA_TARGET_INFO]) {
		uint32_t len = mnl_attr_get_len(tb[NFTA_TARGET_INFO]);
		void *target_data;

		if (target->data)
			free((void *) target->data);

		target_data = calloc(1, len);
		if (target_data == NULL)
			return -1;

		memcpy(target_data, mnl_attr_get_payload(tb[NFTA_TARGET_INFO]), len);

		target->data = target_data;
		target->data_len = len;

		e->flags |= (1 << NFT_EXPR_TG_INFO);
	}

	return 0;
}

static int
nft_rule_expr_target_snprintf(char *buf, size_t len, struct nft_rule_expr *e)
{
	struct nft_expr_target *target = (struct nft_expr_target *)e->data;

	return snprintf(buf, len, "name=%s rev=%u ",
			target->name, target->rev);
}

struct expr_ops expr_ops_target = {
	.name		= "target",
	.alloc_len	= sizeof(struct nft_expr_target),
	.max_attr	= NFTA_TARGET_MAX,
	.set		= nft_rule_expr_target_set,
	.get		= nft_rule_expr_target_get,
	.parse		= nft_rule_expr_target_parse,
	.build		= nft_rule_expr_target_build,
	.snprintf	= nft_rule_expr_target_snprintf,
};