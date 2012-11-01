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

#include <time.h>
#include <endian.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <netinet/in.h>

#include <libmnl/libmnl.h>
#include <linux/netfilter/nfnetlink.h>
#include <linux/netfilter/nf_tables.h>

#include <libnftables/rule.h>
#include <libnftables/expr.h>

#include "linux_list.h"
#include "expr_ops.h"

struct nft_rule {
	struct list_head head;

	uint32_t	flags;
	char		*table;
	char		*chain;
	uint8_t		family;
	uint16_t	handle;

	struct list_head expr_list;
};

struct nft_rule *nft_rule_alloc(void)
{
	struct nft_rule *r;

	r = calloc(1, sizeof(struct nft_rule));
	if (r == NULL)
		return NULL;

	INIT_LIST_HEAD(&r->expr_list);

	return r;
}
EXPORT_SYMBOL(nft_rule_alloc);

void nft_rule_free(struct nft_rule *r)
{
	if (r->table != NULL)
		free(r->table);
	if (r->chain != NULL)
		free(r->chain);

	free(r);
}
EXPORT_SYMBOL(nft_rule_free);

void nft_rule_attr_set(struct nft_rule *r, uint16_t attr, void *data)
{
	switch(attr) {
	case NFT_RULE_ATTR_TABLE:
		if (r->table)
			free(r->table);

		r->table = strdup(data);
		break;
	case NFT_RULE_ATTR_CHAIN:
		if (r->chain)
			free(r->chain);

		r->chain = strdup(data);
		break;
	case NFT_RULE_ATTR_HANDLE:
		r->handle = *((uint16_t *)data);
		break;
	default:
		return;
	}
	r->flags |= (1 << attr);
}
EXPORT_SYMBOL(nft_rule_attr_set);

void nft_rule_attr_set_u16(struct nft_rule *r, uint16_t attr, uint16_t val)
{
	nft_rule_attr_set(r, attr, &val);
}
EXPORT_SYMBOL(nft_rule_attr_set_u16);

void nft_rule_attr_set_str(struct nft_rule *r, uint16_t attr, char *str)
{
	nft_rule_attr_set(r, attr, str);
}
EXPORT_SYMBOL(nft_rule_attr_set_str);

void *nft_rule_attr_get(struct nft_rule *r, uint16_t attr)
{
	switch(attr) {
	case NFT_RULE_ATTR_TABLE:
		if (r->flags & (1 << NFT_RULE_ATTR_TABLE))
			return r->table;
		else
			return NULL;
		break;
	case NFT_RULE_ATTR_CHAIN:
		if (r->flags & (1 << NFT_RULE_ATTR_CHAIN))
			return r->chain;
		else
			return NULL;
	case NFT_RULE_ATTR_HANDLE:
		if (r->flags & (1 << NFT_RULE_ATTR_HANDLE))
			return &r->handle;
		else
			return NULL;
		break;
	default:
		return NULL;
	}
}
EXPORT_SYMBOL(nft_rule_attr_get);

const char *nft_rule_attr_get_str(struct nft_rule *r, uint16_t attr)
{
	return nft_rule_attr_get(r, attr);
}
EXPORT_SYMBOL(nft_rule_attr_get_str);

uint16_t nft_rule_attr_get_u16(struct nft_rule *r, uint16_t attr)
{
	uint16_t val = *((uint32_t *)nft_rule_attr_get(r, attr));
	return val;
}
EXPORT_SYMBOL(nft_rule_attr_get_u16);

struct nlmsghdr *
nft_rule_nlmsg_build_hdr(char *buf, uint16_t cmd, uint16_t family,
			  uint16_t type, uint32_t seq)
{
	struct nlmsghdr *nlh;
	struct nfgenmsg *nfh;

	nlh = mnl_nlmsg_put_header(buf);
	nlh->nlmsg_type = (NFNL_SUBSYS_NFTABLES << 8) | cmd;
	nlh->nlmsg_flags = NLM_F_REQUEST | type;
	nlh->nlmsg_seq = seq;

	nfh = mnl_nlmsg_put_extra_header(nlh, sizeof(struct nfgenmsg));
	nfh->nfgen_family = family;
	nfh->version = NFNETLINK_V0;
	nfh->res_id = 0;

	return nlh;
}
EXPORT_SYMBOL(nft_rule_nlmsg_build_hdr);

void nft_rule_nlmsg_build_payload(struct nlmsghdr *nlh, struct nft_rule *r)
{
	struct nft_rule_expr *expr;
	struct nlattr *nest;

	if (r->flags & (1 << NFT_RULE_ATTR_TABLE))
		mnl_attr_put_strz(nlh, NFTA_RULE_TABLE, r->table);
	if (r->flags & (1 << NFT_RULE_ATTR_CHAIN))
		mnl_attr_put_strz(nlh, NFTA_RULE_CHAIN, r->chain);
	if (r->flags & (1 << NFT_RULE_ATTR_HANDLE))
		mnl_attr_put_u16(nlh, NFTA_RULE_HANDLE, htons(r->handle));

	nest = mnl_attr_nest_start(nlh, NFTA_RULE_EXPRESSIONS);
	list_for_each_entry(expr, &r->expr_list, head) {
		nft_rule_expr_build_payload(nlh, expr);
	}
	mnl_attr_nest_end(nlh, nest);
}
EXPORT_SYMBOL(nft_rule_nlmsg_build_payload);

void nft_rule_add_expr(struct nft_rule *r, struct nft_rule_expr *expr)
{
	list_add_tail(&expr->head, &r->expr_list);
}
EXPORT_SYMBOL(nft_rule_add_expr);

static int nft_rule_parse_attr_cb(const struct nlattr *attr, void *data)
{
	const struct nlattr **tb = data;
	int type = mnl_attr_get_type(attr);

	if (mnl_attr_type_valid(attr, NFTA_RULE_MAX) < 0)
		return MNL_CB_OK;

	switch(type) {
	case NFTA_RULE_TABLE:
	case NFTA_RULE_CHAIN:
		if (mnl_attr_validate(attr, MNL_TYPE_STRING) < 0) {
			perror("mnl_attr_validate");
			return MNL_CB_ERROR;
		}
		break;
	case NFTA_RULE_HANDLE:
		if (mnl_attr_validate(attr, MNL_TYPE_U16) < 0) {
			perror("mnl_attr_validate");
			return MNL_CB_ERROR;
		}
		break;
	}

	tb[type] = attr;
	return MNL_CB_OK;
}

static int nft_rule_parse_expr_cb(const struct nlattr *attr, void *data)
{
	const struct nlattr **tb = data;
	int type = mnl_attr_get_type(attr);

	if (mnl_attr_type_valid(attr, NFTA_EXPR_MAX) < 0)
		return MNL_CB_OK;

	switch(type) {
	case NFTA_EXPR_NAME:
		if (mnl_attr_validate(attr, MNL_TYPE_STRING) < 0) {
			perror("mnl_attr_validate");
			return MNL_CB_ERROR;
		}
		break;
	case NFTA_EXPR_DATA:
		if (mnl_attr_validate(attr, MNL_TYPE_NESTED) < 0) {
			perror("mnl_attr_validate");
			return MNL_CB_ERROR;
		}
		break;
	}

	tb[type] = attr;
	return MNL_CB_OK;
}

static int nft_rule_parse_expr2(struct nlattr *attr, struct nft_rule *r)
{
	struct nlattr *tb[NFTA_EXPR_MAX+1] = {};
	struct nft_rule_expr *expr;

	if (mnl_attr_parse_nested(attr, nft_rule_parse_expr_cb, tb) < 0)
		return -1;

	expr = nft_rule_expr_alloc(mnl_attr_get_str(tb[NFTA_EXPR_NAME]));
	if (expr == NULL)
		return -1;

	if (tb[NFTA_EXPR_DATA]) {
		if (expr->ops->parse(expr, tb[NFTA_EXPR_DATA]) < 0) {
			free(expr);
			return -1;
		}
	}
	list_add_tail(&expr->head, &r->expr_list);

	return 0;
}

static int nft_rule_parse_expr(struct nlattr *nest, struct nft_rule *r)
{
	struct nlattr *attr;

	mnl_attr_for_each_nested(attr, nest) {
		if (mnl_attr_get_type(attr) != NFTA_LIST_ELEM)
			return -1;

		nft_rule_parse_expr2(attr, r);
	}
	return 0;
}

int nft_rule_nlmsg_parse(const struct nlmsghdr *nlh, struct nft_rule *r)
{
	struct nlattr *tb[NFTA_RULE_MAX+1] = {};
	struct nfgenmsg *nfg = mnl_nlmsg_get_payload(nlh);
	int ret = 0;

	mnl_attr_parse(nlh, sizeof(*nfg), nft_rule_parse_attr_cb, tb);
	if (tb[NFTA_RULE_TABLE]) {
		r->table = strdup(mnl_attr_get_str(tb[NFTA_RULE_TABLE]));
		r->flags |= (1 << NFT_RULE_ATTR_TABLE);
	}
	if (tb[NFTA_RULE_CHAIN]) {
		r->chain = strdup(mnl_attr_get_str(tb[NFTA_RULE_CHAIN]));
		r->flags |= (1 << NFT_RULE_ATTR_CHAIN);
	}
	if (tb[NFTA_RULE_HANDLE]) {
		r->handle = ntohs(mnl_attr_get_u16(tb[NFTA_RULE_HANDLE]));
		r->flags |= (1 << NFT_RULE_ATTR_HANDLE);
	}
	if (tb[NFTA_RULE_EXPRESSIONS])
		ret = nft_rule_parse_expr(tb[NFTA_RULE_EXPRESSIONS], r);

	r->family = nfg->nfgen_family;

	return ret;
}
EXPORT_SYMBOL(nft_rule_nlmsg_parse);

int nft_rule_snprintf(char *buf, size_t size, struct nft_rule *r,
		       uint32_t type, uint32_t flags)
{
	int ret;
	struct nft_rule_expr *expr;
	int len = size, offset = 0;

	ret = snprintf(buf, size, "family=%u table=%s chain=%s handle=%u ",
			r->family, r->table, r->chain, r->handle);
	SNPRINTF_BUFFER_SIZE(ret, size, len, offset);

	list_for_each_entry(expr, &r->expr_list, head) {
		ret = snprintf(buf+offset, len, "%s ", expr->ops->name);
		SNPRINTF_BUFFER_SIZE(ret, size, len, offset);

		ret = expr->ops->snprintf(buf+offset, len, expr);
		SNPRINTF_BUFFER_SIZE(ret, size, len, offset);
	}
	ret = snprintf(buf+offset-1, len, "\n");
	SNPRINTF_BUFFER_SIZE(ret, size, len, offset);

	return ret;
}
EXPORT_SYMBOL(nft_rule_snprintf);

struct nft_rule_expr_iter {
	struct nft_rule		*r;
	struct nft_rule_expr	*cur;
};

struct nft_rule_expr_iter *nft_rule_expr_iter_create(struct nft_rule *r)
{
	struct nft_rule_expr_iter *iter;

	iter = calloc(1, sizeof(struct nft_rule_expr_iter));
	if (iter == NULL)
		return NULL;

	iter->r = r;
	iter->cur = list_entry(r->expr_list.next, struct nft_rule_expr, head);

	return iter;
}
EXPORT_SYMBOL(nft_rule_expr_iter_create);

struct nft_rule_expr *nft_rule_expr_iter_next(struct nft_rule_expr_iter *iter)
{
	struct nft_rule_expr *expr = iter->cur;

	/* get next expression, if any */
	iter->cur = list_entry(iter->cur->head.next, struct nft_rule_expr, head);
	if (&iter->cur->head == iter->r->expr_list.next)
		return NULL;

	return expr;
}
EXPORT_SYMBOL(nft_rule_expr_iter_next);

void nft_rule_expr_iter_destroy(struct nft_rule_expr_iter *iter)
{
	free(iter);
}
EXPORT_SYMBOL(nft_rule_expr_iter_destroy);

struct nft_rule_list {
	struct list_head list;
};

struct nft_rule_list *nft_rule_list_alloc(void)
{
	struct nft_rule_list *list;

	list = calloc(1, sizeof(struct nft_rule_list));
	if (list == NULL)
		return NULL;

	INIT_LIST_HEAD(&list->list);

	return list;
}
EXPORT_SYMBOL(nft_rule_list_alloc);

void nft_rule_list_free(struct nft_rule_list *list)
{
	struct nft_rule *r, *tmp;

	list_for_each_entry_safe(r, tmp, &list->list, head) {
		list_del(&r->head);
		nft_rule_free(r);
	}
	free(list);
}
EXPORT_SYMBOL(nft_rule_list_free);

void nft_rule_list_add(struct nft_rule *r, struct nft_rule_list *list)
{
	list_add_tail(&r->head, &list->list);
}
EXPORT_SYMBOL(nft_rule_list_add);

struct nft_rule_list_iter {
	struct nft_rule_list	*list;
	struct nft_rule		*cur;
};

struct nft_rule_list_iter *nft_rule_list_iter_create(struct nft_rule_list *l)
{
	struct nft_rule_list_iter *iter;

	iter = calloc(1, sizeof(struct nft_rule_list_iter));
	if (iter == NULL)
		return NULL;

	iter->list = l;
	iter->cur = list_entry(l->list.next, struct nft_rule, head);

	return iter;
}
EXPORT_SYMBOL(nft_rule_list_iter_create);

struct nft_rule *nft_rule_list_iter_cur(struct nft_rule_list_iter *iter)
{
	return iter->cur;
}
EXPORT_SYMBOL(nft_rule_list_iter_cur);

struct nft_rule *nft_rule_list_iter_next(struct nft_rule_list_iter *iter)
{
	struct nft_rule *r = iter->cur;

	/* get next rule, if any */
	iter->cur = list_entry(iter->cur->head.next, struct nft_rule, head);
	if (&iter->cur->head == iter->list->list.next)
		return NULL;

	return r;
}
EXPORT_SYMBOL(nft_rule_list_iter_next);

void nft_rule_list_iter_destroy(struct nft_rule_list_iter *iter)
{
	free(iter);
}
EXPORT_SYMBOL(nft_rule_list_iter_destroy);