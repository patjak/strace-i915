/*
 * Copyright (c) 2016 Fabien Siron <fabien.siron@epita.fr>
 * Copyright (c) 2017 JingPiao Chen <chenjingpiao@gmail.com>
 * Copyright (c) 2017-2018 The strace developers.
 * All rights reserved.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "defs.h"
#include "netlink.h"
#include "netlink_sock_diag.h"
#include "nlattr.h"
#include "print_fields.h"

#include <linux/filter.h>
#include <linux/sock_diag.h>
#include <linux/packet_diag.h>

#include "xlat/af_packet_versions.h"
#include "xlat/packet_diag_attrs.h"
#include "xlat/packet_diag_info_flags.h"
#include "xlat/packet_diag_show.h"

DECL_NETLINK_DIAG_DECODER(decode_packet_diag_req)
{
	struct packet_diag_req req = { .sdiag_family = family };
	const size_t offset = sizeof(req.sdiag_family);

	PRINT_FIELD_XVAL("{", req, sdiag_family, addrfams, "AF_???");
	tprints(", ");
	if (len >= sizeof(req)) {
		if (!umoven_or_printaddr(tcp, addr + offset,
					 sizeof(req) - offset,
					 (char *) &req + offset)) {
			/*
			 * AF_PACKET currently doesn't support protocol values
			 * other than 0.
			 */
			PRINT_FIELD_X("", req, sdiag_protocol);
			PRINT_FIELD_U(", ", req, pdiag_ino);
			PRINT_FIELD_FLAGS(", ", req, pdiag_show,
					  packet_diag_show, "PACKET_SHOW_???");
			PRINT_FIELD_COOKIE(", ", req, pdiag_cookie);
		}
	} else
		tprints("...");
	tprints("}");
}

static bool
decode_packet_diag_info(struct tcb *const tcp,
			const kernel_ulong_t addr,
			const unsigned int len,
			const void *const opaque_data)
{
	struct packet_diag_info pinfo;

	if (len < sizeof(pinfo))
		return false;
	if (umove_or_printaddr(tcp, addr, &pinfo))
		return true;

	PRINT_FIELD_IFINDEX("{", pinfo, pdi_index);
	PRINT_FIELD_XVAL(", ", pinfo, pdi_version, af_packet_versions,
			 "TPACKET_???");
	PRINT_FIELD_U(", ", pinfo, pdi_reserve);
	PRINT_FIELD_U(", ", pinfo, pdi_copy_thresh);
	PRINT_FIELD_U(", ", pinfo, pdi_tstamp);
	PRINT_FIELD_FLAGS(", ", pinfo, pdi_flags,
			  packet_diag_info_flags, "PDI_???");
	tprints("}");

	return true;
}

static bool
print_packet_diag_mclist(struct tcb *const tcp, void *const elem_buf,
			 const size_t elem_size, void *const opaque_data)
{
	struct packet_diag_mclist *dml = elem_buf;
	uint16_t alen = MIN(dml->pdmc_alen, sizeof(dml->pdmc_addr));

	PRINT_FIELD_IFINDEX("{", *dml, pdmc_index);
	PRINT_FIELD_U(", ", *dml, pdmc_count);
	PRINT_FIELD_U(", ", *dml, pdmc_type);
	PRINT_FIELD_U(", ", *dml, pdmc_alen);
	PRINT_FIELD_STRING(", ", *dml, pdmc_addr, alen, QUOTE_FORCE_HEX);
	tprints("}");

	return true;
}

static bool
decode_packet_diag_mclist(struct tcb *const tcp,
			  const kernel_ulong_t addr,
			  const unsigned int len,
			  const void *const opaque_data)
{
	struct packet_diag_mclist dml;
	const size_t nmemb = len / sizeof(dml);

	if (!nmemb)
		return false;

	print_array(tcp, addr, nmemb, &dml, sizeof(dml),
		    tfetch_mem, print_packet_diag_mclist, 0);

	return true;
}

static bool
decode_packet_diag_ring(struct tcb *const tcp,
			const kernel_ulong_t addr,
			const unsigned int len,
			const void *const opaque_data)
{
	struct packet_diag_ring pdr;

	if (len < sizeof(pdr))
		return false;
	if (umove_or_printaddr(tcp, addr, &pdr))
		return true;

	PRINT_FIELD_U("{", pdr, pdr_block_size);
	PRINT_FIELD_U(", ", pdr, pdr_block_nr);
	PRINT_FIELD_U(", ", pdr, pdr_frame_size);
	PRINT_FIELD_U(", ", pdr, pdr_frame_nr);
	PRINT_FIELD_U(", ", pdr, pdr_retire_tmo);
	PRINT_FIELD_U(", ", pdr, pdr_sizeof_priv);
	PRINT_FIELD_U(", ", pdr, pdr_features);
	tprints("}");

	return true;
}

static bool
decode_packet_diag_filter(struct tcb *const tcp,
			  const kernel_ulong_t addr,
			  const unsigned int len,
			  const void *const opaque_data)
{
	const unsigned int nmemb = len / sizeof(struct sock_filter);
	if (!nmemb || (unsigned short) nmemb != nmemb)
		return false;

	print_sock_fprog(tcp, addr, nmemb);

	return true;
}

static const nla_decoder_t packet_diag_msg_nla_decoders[] = {
	[PACKET_DIAG_INFO]	= decode_packet_diag_info,
	[PACKET_DIAG_MCLIST]	= decode_packet_diag_mclist,
	[PACKET_DIAG_RX_RING]	= decode_packet_diag_ring,
	[PACKET_DIAG_TX_RING]	= decode_packet_diag_ring,
	[PACKET_DIAG_FANOUT]	= decode_nla_u32,
	[PACKET_DIAG_UID]	= decode_nla_uid,
	[PACKET_DIAG_MEMINFO]	= decode_nla_meminfo,
	[PACKET_DIAG_FILTER]	= decode_packet_diag_filter
};

DECL_NETLINK_DIAG_DECODER(decode_packet_diag_msg)
{
	struct packet_diag_msg msg = { .pdiag_family = family };
	size_t offset = sizeof(msg.pdiag_family);
	bool decode_nla = false;

	PRINT_FIELD_XVAL("{", msg, pdiag_family, addrfams, "AF_???");
	tprints(", ");
	if (len >= sizeof(msg)) {
		if (!umoven_or_printaddr(tcp, addr + offset,
					 sizeof(msg) - offset,
					 (char *) &msg + offset)) {
			PRINT_FIELD_XVAL("", msg, pdiag_type,
					 socktypes, "SOCK_???");
			PRINT_FIELD_XVAL_SORTED_SIZED(", ", msg, pdiag_num,
						      ethernet_protocols,
						      ethernet_protocols_size,
						      "ETH_P_???");
			PRINT_FIELD_U(", ", msg, pdiag_ino);
			PRINT_FIELD_COOKIE(", ", msg, pdiag_cookie);
			decode_nla = true;
		}
	} else
		tprints("...");
	tprints("}");

	offset = NLMSG_ALIGN(sizeof(msg));
	if (decode_nla && len > offset) {
		tprints(", ");
		decode_nlattr(tcp, addr + offset, len - offset,
			      packet_diag_attrs, "PACKET_DIAG_???",
			      packet_diag_msg_nla_decoders,
			      ARRAY_SIZE(packet_diag_msg_nla_decoders), NULL);
	}
}
