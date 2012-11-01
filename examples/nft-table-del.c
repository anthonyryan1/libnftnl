/*
 * (C) 2012 by Pablo Neira Ayuso <pablo@netfilter.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This software has been sponsored by Sophos Astaro <http://www.sophos.com>
 */

#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <netinet/in.h>

#include <linux/netfilter/nf_tables.h>

#include <libmnl/libmnl.h>
#include <libnftables/table.h>

int main(int argc, char *argv[])
{
	struct mnl_socket *nl;
	char buf[MNL_SOCKET_BUFFER_SIZE];
	struct nlmsghdr *nlh;
	uint32_t portid, seq, family;
	struct nft_table *t = NULL;
	int ret;

	if (argc != 3) {
		fprintf(stderr, "%s <family> <name>\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	t = nft_table_alloc();
	if (t == NULL) {
		perror("OOM");
		exit(EXIT_FAILURE);
	}

	seq = time(NULL);
	if (strcmp(argv[1], "ip") == 0)
		family = AF_INET;
	else if (strcmp(argv[1], "ip6") == 0)
		family = AF_INET6;
	else if (strcmp(argv[1], "bridge") == 0)
		family = AF_BRIDGE;
	else {
		fprintf(stderr, "Unknown family: ip, ip6, bridge\n");
		exit(EXIT_FAILURE);
	}

	nlh = nft_table_nlmsg_build_hdr(buf, NFT_MSG_DELTABLE, family,
					NLM_F_ACK, seq);
	nft_table_attr_set(t, NFT_TABLE_ATTR_NAME, argv[2]);

	nft_table_nlmsg_build_payload(nlh, t);
	nft_table_free(t);

	nl = mnl_socket_open(NETLINK_NETFILTER);
	if (nl == NULL) {
		perror("mnl_socket_open");
		exit(EXIT_FAILURE);
	}

	if (mnl_socket_bind(nl, 0, MNL_SOCKET_AUTOPID) < 0) {
		perror("mnl_socket_bind");
		exit(EXIT_FAILURE);
	}
	portid = mnl_socket_get_portid(nl);

	if (mnl_socket_sendto(nl, nlh, nlh->nlmsg_len) < 0) {
		perror("mnl_socket_send");
		exit(EXIT_FAILURE);
	}

	ret = mnl_socket_recvfrom(nl, buf, sizeof(buf));
	while (ret > 0) {
		ret = mnl_cb_run(buf, ret, seq, portid, NULL, NULL);
		if (ret <= 0)
			break;
		ret = mnl_socket_recvfrom(nl, buf, sizeof(buf));
	}
	if (ret == -1) {
		perror("error");
		exit(EXIT_FAILURE);
	}
	mnl_socket_close(nl);

	return EXIT_SUCCESS;
}