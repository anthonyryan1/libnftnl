/*
 * (C) 2012-2013 by Pablo Neira Ayuso <pablo@netfilter.org>
 * (C) 2013 by Arturo Borrero Gonzalez <arturo.borrero.glez@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This code has been sponsored by Sophos Astaro <http://www.sophos.com>
 */
#include "internal.h"
#include "expr_ops.h"
#include <stdint.h>
#include <limits.h>

#include <linux/netfilter/nf_tables.h>
#include <libnftables/rule.h>
#include <libnftables/expr.h>
#include <libnftables/set.h>

#ifdef XML_PARSING
struct nft_rule_expr *nft_mxml_expr_parse(mxml_node_t *node)
{
	mxml_node_t *tree;
	struct nft_rule_expr *e;
	const char *expr_name;
	char *xml_text;
	int ret;

	expr_name = mxmlElementGetAttr(node, "type");
	if (expr_name == NULL)
		goto err;

	e = nft_rule_expr_alloc(expr_name);
	if (e == NULL)
		goto err;

	xml_text = mxmlSaveAllocString(node, MXML_NO_CALLBACK);
	if (xml_text == NULL)
		goto err_expr;

	tree = mxmlLoadString(NULL, xml_text, MXML_OPAQUE_CALLBACK);
	xfree(xml_text);

	if (tree == NULL)
		goto err_expr;

	ret = e->ops->xml_parse(e, tree);
	mxmlDelete(tree);

	return ret < 0 ? NULL : e;
err_expr:
	nft_rule_expr_free(e);
err:
	mxmlDelete(tree);
	errno = EINVAL;
	return NULL;
}

int nft_mxml_reg_parse(mxml_node_t *tree, const char *reg_name, uint32_t flags)
{
	mxml_node_t *node;
	uint64_t val;

	node = mxmlFindElement(tree, tree, reg_name, NULL, NULL, flags);
	if (node == NULL) {
		errno = EINVAL;
		goto err;
	}

	if (nft_strtoi(node->child->value.opaque, BASE_DEC, &val,
		       NFT_TYPE_U64) != 0)
		goto err;

	if (val > NFT_REG_MAX) {
		errno = ERANGE;
		goto err;
	}
	return val;
err:
	return -1;
}

int nft_mxml_data_reg_parse(mxml_node_t *tree, const char *node_name,
			    union nft_data_reg *data_reg)
{
	mxml_node_t *node;
	const char *type;
	char *tmpstr = NULL;
	int ret;

	node = mxmlFindElement(tree, tree, node_name, NULL, NULL,
			       MXML_DESCEND_FIRST);
	if (node == NULL || node->child == NULL) {
		errno = EINVAL;
		goto err;
	}

	tmpstr = mxmlSaveAllocString(node, MXML_NO_CALLBACK);
	if (tmpstr == NULL) {
		errno = ENOMEM;
		goto err;
	}

	ret = nft_data_reg_xml_parse(data_reg, tmpstr);
	xfree(tmpstr);

	if (ret < 0) {
		errno = EINVAL;
		goto err;
	}

	node = mxmlFindElement(node, node, "data_reg", NULL, NULL,
			       MXML_DESCEND);
	if (node == NULL || node->child == NULL) {
		errno = EINVAL;
		goto err;
	}

	type = mxmlElementGetAttr(node, "type");
	if (type == NULL) {
		errno = EINVAL;
		goto err;
	}

	if (strcmp(type, "value") == 0)
		return DATA_VALUE;
	else if (strcmp(type, "verdict") == 0)
		return DATA_VERDICT;
	else if (strcmp(type, "chain") == 0)
		return DATA_CHAIN;
	else
		errno = EINVAL;
err:
	return -1;
}

int
nft_mxml_num_parse(mxml_node_t *tree, const char *node_name,
		   uint32_t mxml_flags, int base, void *number,
		   enum nft_type type)
{
	mxml_node_t *node = NULL;

	node = mxmlFindElement(tree, tree, node_name, NULL, NULL, mxml_flags);
	if (node == NULL || node->child == NULL) {
		errno = EINVAL;
		return -1;
	}

	return nft_strtoi(node->child->value.opaque, base, number, type);
}

const char *nft_mxml_str_parse(mxml_node_t *tree, const char *node_name,
			       uint32_t mxml_flags)
{
	mxml_node_t *node;

	node = mxmlFindElement(tree, tree, node_name, NULL, NULL, mxml_flags);
	if (node == NULL || node->child == NULL) {
		errno = EINVAL;
		return NULL;
	}

	return strdup(node->child->value.opaque);
}

int nft_mxml_family_parse(mxml_node_t *tree, const char *node_name,
			  uint32_t mxml_flags)
{
	const char *family_str;
	int family;

	family_str = nft_mxml_str_parse(tree, node_name, mxml_flags);
	if (family_str == NULL)
		return -1;

	family = nft_str2family(family_str);
	xfree(family_str);

	if (family < 0)
		errno = EAFNOSUPPORT;

	return family;
}

struct nft_set_elem *nft_mxml_set_elem_parse(mxml_node_t *node)
{
	mxml_node_t *save;
	char *set_elem_str;
	struct nft_set_elem *elem;

	if (node == NULL)
		goto einval;

	if (strcmp(node->value.opaque, "set_elem") != 0)
		goto einval;

	elem = nft_set_elem_alloc();
	if (elem == NULL)
		goto enomem;

	/* This is a hack for mxml to print just the current node */
	save = node->next;
	node->next = NULL;

	set_elem_str = mxmlSaveAllocString(node, MXML_NO_CALLBACK);
	node->next = save;

	if (set_elem_str == NULL) {
		xfree(elem);
		goto enomem;
	}

	if (nft_set_elem_parse(elem, NFT_SET_PARSE_XML,
			       set_elem_str) != 0) {
		xfree(set_elem_str);
		xfree(elem);
		return NULL;
	}

	xfree(set_elem_str);

	return elem;
einval:
	errno = EINVAL;
	return NULL;
enomem:
	errno = ENOMEM;
	return NULL;
}
#endif
