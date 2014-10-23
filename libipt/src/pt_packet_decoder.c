/*
 * Copyright (c) 2014, Intel Corporation
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *  * Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *  * Neither the name of Intel Corporation nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "pt_packet_decoder.h"
#include "pt_decoder_function.h"
#include "pt_packet.h"
#include "pt_sync.h"

#include <string.h>


int pt_pkt_decoder_init(struct pt_packet_decoder *decoder,
			const struct pt_config *config)
{
	const uint8_t *begin, *end;

	if (!decoder || !config)
		return -pte_invalid;

	if (config->size != sizeof(*config))
		return -pte_bad_config;

	begin = config->begin;
	end = config->end;

	if (!begin || end < begin)
		return -pte_bad_config;

	memset(decoder, 0, sizeof(*decoder));
	decoder->config = *config;

	return 0;
}

struct pt_packet_decoder *pt_pkt_alloc_decoder(const struct pt_config *config)
{
	struct pt_packet_decoder *decoder;
	int errcode;

	decoder = malloc(sizeof(*decoder));
	if (!decoder)
		return NULL;

	errcode = pt_pkt_decoder_init(decoder, config);
	if (errcode < 0) {
		free(decoder);
		return NULL;
	}

	return decoder;
}

void pt_pkt_decoder_fini(struct pt_packet_decoder *decoder)
{
	/* Nothing to do. */
}

void pt_pkt_free_decoder(struct pt_packet_decoder *decoder)
{
	pt_pkt_decoder_fini(decoder);
	free(decoder);
}

int pt_pkt_sync_forward(struct pt_packet_decoder *decoder)
{
	const uint8_t *pos, *sync;
	int errcode;

	if (!decoder)
		return -pte_invalid;

	sync = decoder->sync;
	pos = decoder->pos;
	if (!pos)
		pos = decoder->config.begin;

	if (pos == sync)
		pos += ptps_psb;

	errcode = pt_sync_forward(&sync, pos, &decoder->config);
	if (errcode < 0)
		return errcode;

	decoder->sync = sync;
	decoder->pos = sync;

	return 0;
}

int pt_pkt_sync_backward(struct pt_packet_decoder *decoder)
{
	const uint8_t *pos, *sync;
	int errcode;

	if (!decoder)
		return -pte_invalid;

	pos = decoder->sync;
	if (!pos)
		pos = decoder->config.end;

	errcode = pt_sync_backward(&sync, pos, &decoder->config);
	if (errcode < 0)
		return errcode;

	decoder->sync = sync;
	decoder->pos = sync;

	return 0;
}

int pt_pkt_sync_set(struct pt_packet_decoder *decoder, uint64_t offset)
{
	const uint8_t *begin, *end, *pos;

	if (!decoder)
		return -pte_invalid;

	begin = decoder->config.begin;
	end = decoder->config.end;
	pos = begin + offset;

	if (end < pos || pos < begin)
		return -pte_invalid;

	decoder->sync = pos;
	decoder->pos = pos;

	return 0;
}

int pt_pkt_get_offset(struct pt_packet_decoder *decoder, uint64_t *offset)
{
	const uint8_t *begin, *pos;

	if (!decoder || !offset)
		return -pte_invalid;

	begin = decoder->config.begin;
	pos = decoder->pos;

	if (!pos)
		return -pte_nosync;

	*offset = pos - begin;
	return 0;
}

int pt_pkt_get_sync_offset(struct pt_packet_decoder *decoder, uint64_t *offset)
{
	const uint8_t *begin, *sync;

	if (!decoder || !offset)
		return -pte_invalid;

	begin = decoder->config.begin;
	sync = decoder->sync;

	if (!sync)
		return -pte_nosync;

	*offset = sync - begin;
	return 0;
}

int pt_pkt_next(struct pt_packet_decoder *decoder, struct pt_packet *packet)
{
	const struct pt_decoder_function *dfun;
	int errcode, size;

	if (!packet || !decoder)
		return -pte_invalid;

	errcode = pt_df_fetch(&dfun, decoder->pos, &decoder->config);
	if (errcode < 0)
		return errcode;

	if (!dfun)
		return -pte_internal;

	if (!dfun->packet)
		return -pte_internal;

	size = dfun->packet(decoder, packet);
	if (size < 0)
		return size;

	decoder->pos += size;

	return size;
}

int pt_pkt_decode_unknown(struct pt_packet_decoder *decoder,
			  struct pt_packet *packet)
{
	int size;

	if (!decoder)
		return -pte_internal;

	size = pt_pkt_read_unknown(packet, decoder->pos, &decoder->config);
	if (size < 0)
		return size;

	return size;
}

int pt_pkt_decode_pad(struct pt_packet_decoder *decoder,
		      struct pt_packet *packet)
{
	if (!packet)
		return -pte_internal;

	packet->type = ppt_pad;
	packet->size = ptps_pad;

	return ptps_pad;
}

int pt_pkt_decode_psb(struct pt_packet_decoder *decoder,
		      struct pt_packet *packet)
{
	int size;

	if (!decoder)
		return -pte_internal;

	size = pt_pkt_read_psb(decoder->pos, &decoder->config);
	if (size < 0)
		return size;

	packet->type = ppt_psb;
	packet->size = (uint8_t) size;

	return size;
}

int pt_pkt_decode_tip(struct pt_packet_decoder *decoder,
		      struct pt_packet *packet)
{
	int size;

	if (!decoder || !packet)
		return -pte_internal;

	size = pt_pkt_read_ip(&packet->payload.ip, decoder->pos,
			      &decoder->config);
	if (size < 0)
		return size;

	packet->type = ppt_tip;
	packet->size = (uint8_t) size;

	return size;
}

int pt_pkt_decode_tnt_8(struct pt_packet_decoder *decoder,
			struct pt_packet *packet)
{
	int size;

	if (!decoder || !packet)
		return -pte_internal;

	size = pt_pkt_read_tnt_8(&packet->payload.tnt, decoder->pos,
				 &decoder->config);
	if (size < 0)
		return size;

	packet->type = ppt_tnt_8;
	packet->size = (uint8_t) size;

	return size;
}

int pt_pkt_decode_tnt_64(struct pt_packet_decoder *decoder,
			 struct pt_packet *packet)
{
	int size;

	if (!decoder || !packet)
		return -pte_internal;

	size = pt_pkt_read_tnt_64(&packet->payload.tnt, decoder->pos,
				  &decoder->config);
	if (size < 0)
		return size;

	packet->type = ppt_tnt_64;
	packet->size = (uint8_t) size;

	return size;
}

int pt_pkt_decode_tip_pge(struct pt_packet_decoder *decoder,
			  struct pt_packet *packet)
{
	int size;

	if (!decoder || !packet)
		return -pte_internal;

	size = pt_pkt_read_ip(&packet->payload.ip, decoder->pos,
			      &decoder->config);
	if (size < 0)
		return size;

	packet->type = ppt_tip_pge;
	packet->size = (uint8_t) size;

	return size;
}

int pt_pkt_decode_tip_pgd(struct pt_packet_decoder *decoder,
			  struct pt_packet *packet)
{
	int size;

	if (!decoder || !packet)
		return -pte_internal;

	size = pt_pkt_read_ip(&packet->payload.ip, decoder->pos,
			      &decoder->config);
	if (size < 0)
		return size;

	packet->type = ppt_tip_pgd;
	packet->size = (uint8_t) size;

	return size;
}

int pt_pkt_decode_fup(struct pt_packet_decoder *decoder,
		      struct pt_packet *packet)
{
	int size;

	if (!decoder || !packet)
		return -pte_internal;

	size = pt_pkt_read_ip(&packet->payload.ip, decoder->pos,
			      &decoder->config);
	if (size < 0)
		return size;

	packet->type = ppt_fup;
	packet->size = (uint8_t) size;

	return size;
}

int pt_pkt_decode_pip(struct pt_packet_decoder *decoder,
		      struct pt_packet *packet)
{
	int size;

	if (!decoder || !packet)
		return -pte_internal;

	size = pt_pkt_read_pip(&packet->payload.pip, decoder->pos,
			       &decoder->config);
	if (size < 0)
		return size;

	packet->type = ppt_pip;
	packet->size = (uint8_t) size;

	return size;
}

int pt_pkt_decode_ovf(struct pt_packet_decoder *decoder,
		      struct pt_packet *packet)
{
	if (!packet)
		return -pte_internal;

	packet->type = ppt_ovf;
	packet->size = ptps_ovf;

	return ptps_ovf;
}

int pt_pkt_decode_mode(struct pt_packet_decoder *decoder,
		       struct pt_packet *packet)
{
	int size;

	if (!decoder || !packet)
		return -pte_internal;

	size = pt_pkt_read_mode(&packet->payload.mode, decoder->pos,
				&decoder->config);
	if (size < 0)
		return size;

	packet->type = ppt_mode;
	packet->size = (uint8_t) size;

	return size;
}

int pt_pkt_decode_psbend(struct pt_packet_decoder *decoder,
			 struct pt_packet *packet)
{
	if (!packet)
		return -pte_internal;

	packet->type = ppt_psbend;
	packet->size = ptps_psbend;

	return ptps_psbend;
}

int pt_pkt_decode_tsc(struct pt_packet_decoder *decoder,
		      struct pt_packet *packet)
{
	int size;

	if (!decoder || !packet)
		return -pte_internal;

	size = pt_pkt_read_tsc(&packet->payload.tsc, decoder->pos,
			       &decoder->config);
	if (size < 0)
		return size;

	packet->type = ppt_tsc;
	packet->size = (uint8_t) size;

	return size;
}

int pt_pkt_decode_cbr(struct pt_packet_decoder *decoder,
		      struct pt_packet *packet)
{
	int size;

	if (!decoder || !packet)
		return -pte_internal;

	size = pt_pkt_read_cbr(&packet->payload.cbr, decoder->pos,
			       &decoder->config);
	if (size < 0)
		return size;

	packet->type = ppt_cbr;
	packet->size = (uint8_t) size;

	return size;
}
