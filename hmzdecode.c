#include <sys/types.h>
#include <sys/errno.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "hmz_int.h"
#include "hmz.h"

static inline unsigned short
buf_decode_code(const struct decode_buf * const buf, const unsigned int length)
{
	return (buf->buf_val << buf->buf_bits) >> (64 - length);
}

static inline void
buf_decode_fill(struct decode_buf * const buf, const unsigned char *data,
    const unsigned bytes)
{
	unsigned long int bswap;

	buf->buf_data = data;
	memcpy(&bswap, data, 8);
	buf->buf_val = __builtin_bswap64(bswap);
	buf->buf_bits -= bytes << 3;
}

static inline void
buf_decode_init(struct decode_buf * const buf,
    const unsigned char * const data, const unsigned int size)
{
	buf->buf_val = 0;
	buf->buf_bits = 0;
	buf->buf_data = data;
	buf->buf_end = data + size - 1 - 8;

	memcpy(&buf->buf_val, data, 8);
	buf->buf_val = __builtin_bswap64(buf->buf_val);
}

static inline unsigned int
buf_decode_read_multi(struct decode_buf * const buf)
{
	const unsigned char *data;
	unsigned int bytes;

	bytes = buf->buf_bits >> 3;
	data = buf->buf_data + bytes;

	if (data >= buf->buf_end)
		return 0;

	buf_decode_fill(buf, data, bytes);

	return 1;
}

static inline unsigned int
buf_decode_read_one(struct decode_buf * const buf, const unsigned int length)
{
	const unsigned char *data;
	unsigned int bytes;
	unsigned int extra;

	if ((64 - buf->buf_bits) >= length)
		return 1;

	if (buf->buf_data == buf->buf_end)
		return buf->buf_bits < 64;

	bytes = buf->buf_bits >> 3;
	data = buf->buf_data + bytes;

	extra = 0;
	if (data >= buf->buf_end) {
		bytes = buf->buf_end - buf->buf_data;
		data = buf->buf_end;
		extra = *(buf->buf_end + 8);
	}

	buf_decode_fill(buf, data, bytes);
	buf->buf_bits += extra;
	buf->buf_val >>= extra;

	return 1;
}

static inline void
buf_decode_consume(struct decode_buf * const buf, const unsigned int bits)
{
	buf->buf_bits += bits;
}

static inline int
buf_decode_end(const struct decode_buf * const buf)
{
	if (buf->buf_data != buf->buf_end || buf->buf_bits != 64)
		return EIO;
	return 0;
}

static inline void
init_state(struct hmz_decode_state * const state,
    const unsigned char * const buffer_in,
    unsigned char * const buffer_out)
{
	state->in = (unsigned char *)buffer_in;
	state->out = buffer_out;
}

unsigned int
hmz_decode_init(struct hmz_decode_state ** const state)
{
	int error;

	error = posix_memalign((void **)state, MEM_ALIGN, sizeof(**state));
	if (error != 0)
		return ENOMEM;

	return 0;
}

static inline unsigned int
decode_lits(struct hmz_decode_state * const state,
    const unsigned int size_in, const unsigned int size_out)
{
	unsigned int size;

	memcpy(&size, state->in, 4);
	state->in += 4;

	if (size > size_in)
		return EIO;
	if (size > size_out)
		return EOVERFLOW;

	memcpy(state->out, state->in, size);
	state->out += size;
	return 0;
}

static inline unsigned int
decode_rle(struct hmz_decode_state * const state, unsigned int size_out)
{
	unsigned int size;
	unsigned char symbol;

	symbol = *(state->in);
	state->in++;
	memcpy(&size, state->in, 4);

	if (size > size_out)
		return EOVERFLOW;

	memset(state->out, symbol, size);
	state->out += size;
	return 0;
}

static inline void
fill_table(struct hmz_decode_state * const state)
{
	unsigned int ilength;
	unsigned int jlength;
	unsigned int klength;
	unsigned int i;
	unsigned int j;
	unsigned int k;
	struct decode entry;
	struct decode *ptr;
	struct decode *iend;
	struct decode *jend;
	struct decode *kend;

	ptr = state->table;
	for (i = 0; i < state->symbol_count; i++) {
		ilength = state->symbols[i].count;
		entry.symbol[0] = state->symbols[i].symbol;
		iend = ptr + (1 << (state->max_length - ilength));
		for (j = 0; j < state->symbol_count; j++) {
			jlength = ilength + state->symbols[j].count;
			if (jlength > state->max_length)
				break;
			entry.symbol[1] = state->symbols[j].symbol;
			jend = ptr + (1 << (state->max_length - jlength));
			for (k = 0; k < state->symbol_count; k++) {
				klength = jlength + state->symbols[k].count;
				if (klength > state->max_length)
					break;
				entry.symbol[2] = state->symbols[k].symbol;
				entry.count = 3;
				entry.length = klength;
				kend = ptr +
				    (1 << (state->max_length - klength));
				while (ptr < kend)
					*ptr++ = entry;
			}
			entry.count = 2;
			entry.length = jlength;
			while (ptr < jend)
				*ptr++ = entry;
		}
		entry.count = 1;
		entry.length = ilength;
		while (ptr < iend)
			*ptr++ = entry;
	}
}

static inline unsigned char *
decode_one(const struct hmz_decode_state * const state,
    struct decode_buf * const buf, const unsigned int length,
    unsigned char * const out, const unsigned int * const lengths)
{
	const struct decode *decode;

	decode = &state->table[buf_decode_code(buf, length)];
	*out = decode->symbol[0];
	buf_decode_consume(buf, lengths[decode->symbol[0]]);
	return out + 1;
}

static inline unsigned char *
decode_multi(const struct hmz_decode_state * const state,
    struct decode_buf * const buf, const unsigned int length,
    unsigned char * const out)
{
	const struct decode *decode;

	decode = &state->table[buf_decode_code(buf, length)];
	memcpy(out, decode->symbol, 4);
	buf_decode_consume(buf, decode->length);
	return out + decode->count;
}

static inline unsigned int
decode_data_single(struct hmz_decode_state * const state,
    const unsigned int size_in, const unsigned int size_out)
{
	unsigned char *out = state->out;
	unsigned char *end = state->out + size_out;
	struct decode_buf buf;
	unsigned int lengths[SYMBOLS];
	unsigned int size;
	const unsigned int length = state->max_length;

	fill_table(state);

	memcpy(&size, state->in, sizeof(size));
	state->in += sizeof(size);

	if (size_in < (sizeof(size) + size))
		return EIO;

	buf_decode_init(&buf, state->in, size);

	while (out < (end-12) && buf_decode_read_multi(&buf)) {
		out = decode_multi(state, &buf, length, out);
		out = decode_multi(state, &buf, length, out);
		out = decode_multi(state, &buf, length, out);
		out = decode_multi(state, &buf, length, out);
	}

	while (out < (end-3) && buf_decode_read_multi(&buf))
		out = decode_multi(state, &buf, length, out);

	memcpy(lengths, state->lengths, sizeof(state->lengths));

	while (out < end && buf_decode_read_one(&buf, length))
		out = decode_one(state, &buf, length, out, lengths);

	state->out = out;
	return buf_decode_end(&buf);
}

static inline unsigned int
decode_data_multi(struct hmz_decode_state * const state,
    const unsigned int size_in, const unsigned int size_out)
{
	struct decode_buf buf1;
	struct decode_buf buf2;
	struct decode_buf buf3;
	struct decode_buf buf4;
	unsigned char *out1;
	unsigned char *out2;
	unsigned char *out3;
	unsigned char *out4;
	const unsigned char *end1;
	const unsigned char *end2;
	const unsigned char *end3;
	const unsigned char *end4;
	unsigned int lengths[SYMBOLS];
	unsigned int sizes[5];
	const unsigned int length = state->max_length;
	unsigned int part;

	fill_table(state);

	memcpy(sizes, state->in, sizeof(sizes));
	state->in += sizeof(sizes);

	if (size_in <
	    (sizeof(sizes) + sizes[1] + sizes[2] + sizes[3] + sizes[4]))
		return EIO;

	part = sizes[0];

	buf_decode_init(&buf1, state->in, sizes[1]);
	state->in += sizes[1];
	buf_decode_init(&buf2, state->in, sizes[2]);
	state->in += sizes[2];
	buf_decode_init(&buf3, state->in, sizes[3]);
	state->in += sizes[3];
	buf_decode_init(&buf4, state->in, sizes[4]);

	out1 = state->out;
	out2 = out1 + part;
	out3 = out2 + part;
	out4 = out3 + part;

	end1 = out1 + part;
	end2 = out2 + part;
	end3 = out3 + part;
	end4 = out4 + (size_out - (3*part));

	while ((out1 < (end1-12)) && (out2 < (end2-12)) &&
	    (out3 < (end3-12)) && (out4 < (end4-12)) &&
	    buf_decode_read_multi(&buf1) &&
	    buf_decode_read_multi(&buf2) &&
	    buf_decode_read_multi(&buf3) &&
	    buf_decode_read_multi(&buf4)) {

		out1 = decode_multi(state, &buf1, length, out1);
		out2 = decode_multi(state, &buf2, length, out2);
		out3 = decode_multi(state, &buf3, length, out3);
		out4 = decode_multi(state, &buf4, length, out4);

		out1 = decode_multi(state, &buf1, length, out1);
		out2 = decode_multi(state, &buf2, length, out2);
		out3 = decode_multi(state, &buf3, length, out3);
		out4 = decode_multi(state, &buf4, length, out4);

		out1 = decode_multi(state, &buf1, length, out1);
		out2 = decode_multi(state, &buf2, length, out2);
		out3 = decode_multi(state, &buf3, length, out3);
		out4 = decode_multi(state, &buf4, length, out4);

		out1 = decode_multi(state, &buf1, length, out1);
		out2 = decode_multi(state, &buf2, length, out2);
		out3 = decode_multi(state, &buf3, length, out3);
		out4 = decode_multi(state, &buf4, length, out4);
	}

	while (out1 < (end1-12) && buf_decode_read_multi(&buf1)) {
		out1 = decode_multi(state, &buf1, length, out1);
		out1 = decode_multi(state, &buf1, length, out1);
		out1 = decode_multi(state, &buf1, length, out1);
		out1 = decode_multi(state, &buf1, length, out1);
	}

	while (out2 < (end2-12) && buf_decode_read_multi(&buf2)) {
		out2 = decode_multi(state, &buf2, length, out2);
		out2 = decode_multi(state, &buf2, length, out2);
		out2 = decode_multi(state, &buf2, length, out2);
		out2 = decode_multi(state, &buf2, length, out2);
	}

	while (out3 < (end3-12) && buf_decode_read_multi(&buf3)) {
		out3 = decode_multi(state, &buf3, length, out3);
		out3 = decode_multi(state, &buf3, length, out3);
		out3 = decode_multi(state, &buf3, length, out3);
		out3 = decode_multi(state, &buf3, length, out3);
	}

	while (out4 < (end4-12) && buf_decode_read_multi(&buf4)) {
		out4 = decode_multi(state, &buf4, length, out4);
		out4 = decode_multi(state, &buf4, length, out4);
		out4 = decode_multi(state, &buf4, length, out4);
		out4 = decode_multi(state, &buf4, length, out4);
	}

	while (out1 < (end1-3) && buf_decode_read_multi(&buf1))
		out1 = decode_multi(state, &buf1, length, out1);

	while (out2 < (end2-3) && buf_decode_read_multi(&buf2))
		out2 = decode_multi(state, &buf2, length, out2);

	while (out3 < (end3-3) && buf_decode_read_multi(&buf3))
		out3 = decode_multi(state, &buf3, length, out3);

	while (out4 < (end4-3) && buf_decode_read_multi(&buf4))
		out4 = decode_multi(state, &buf4, length, out4);

	memcpy(lengths, state->lengths, sizeof(lengths));

	while (out1 < end1 && buf_decode_read_one(&buf1, length))
		out1 = decode_one(state, &buf1, length, out1, lengths);

	while (out2 < end2 && buf_decode_read_one(&buf2, length))
		out2 = decode_one(state, &buf2, length, out2, lengths);

	while (out3 < end3 && buf_decode_read_one(&buf3, length))
		out3 = decode_one(state, &buf3, length, out3, lengths);

	while (out4 < end4 && buf_decode_read_one(&buf4, length))
		out4 = decode_one(state, &buf4, length, out4, lengths);

	state->out = out4;
	return buf_decode_end(&buf1) | buf_decode_end(&buf2) |
	    buf_decode_end(&buf3) | buf_decode_end(&buf4);
}

static inline unsigned int
decode_data(struct hmz_decode_state * const state,
    const unsigned int size_in, const unsigned int size_out)
{
	if (state->format == HMZ_FMT_SINGLE)
		return decode_data_single(state, size_in, size_out);
	else
		return decode_data_multi(state, size_in, size_out);
}

static inline unsigned int
decode_lens(struct hmz_decode_state * const state, const unsigned int size_in,
    const unsigned int size_out)
{
	const unsigned char *in = state->in;
	unsigned int max_symbol;
	unsigned int i;
	unsigned int val;
	unsigned int length;
	unsigned int index;
	unsigned int header_size;

	max_symbol = *in++;

	memset(state->code_counts, 0, sizeof(state->code_counts));

	for (i = 0; i <= max_symbol; i += 2) {
		val = *in++;
		length = val >> 4;
		state->lengths[i] = length;
		state->code_counts[length]++;
		length = val & 0xF;
		state->lengths[i + 1] = length;
		state->code_counts[length]++;
	}

	state->next_index[1] = 0;
	for (i = 2; i <= state->max_length; i++)
		state->next_index[i] = state->next_index[i - 1] +
		    state->code_counts[i - 1];

	state->next_index[0] = state->next_index[i - 1] +
	    state->code_counts[i - 1];

	state->symbol_count = state->next_index[0];

	for (i = 0; i <= max_symbol; i++) {
		length = state->lengths[i];
		index = state->next_index[length];
		state->symbols[index].symbol = i;
		state->symbols[index].count = length;
		state->next_index[length]++;
	}

	header_size = in - state->in;
	state->in = in;

	return decode_data(state, size_in - header_size, size_out);
}

static inline unsigned int
decode_canon(struct hmz_decode_state * const state, const unsigned int size_in,
    const unsigned int size_out)
{
	const unsigned char *in = state->in;
	unsigned int val;
	unsigned int i;
	unsigned int j;
	unsigned int k;
	unsigned int symbol;
	unsigned int header_size;

	state->code_counts[0] = 0;
	for (i = 1; i <= state->max_length; i++)
		state->code_counts[i] = *in++;

	k = 0;
	for (i = 1; i <= state->max_length; i++) {
		val = state->code_counts[i];
		for (j = 0; j < val; j++) {
			symbol = *in++;
			state->symbols[k].symbol = symbol;
			state->symbols[k].count = i;
			state->lengths[symbol] = i;
			k++;
		}
	}

	state->symbol_count = k;

	header_size = in - state->in;
	state->in = in;

	return decode_data(state, size_in - header_size, size_out);
}

unsigned int
hmz_decode(struct hmz_decode_state * const state,
    const unsigned char * const buffer_in, const unsigned int size_in,
    unsigned char * const buffer_out, unsigned int * const size_out)
{
	unsigned int tag;
	unsigned int error;

	if (state == NULL ||
	    buffer_in == NULL || size_in < MIN_HEADER_SIZE ||
	    buffer_out == NULL || *size_out == 0)
		return EINVAL;

	init_state(state, buffer_in, buffer_out);

	tag = *state->in;
	state->format = (tag >> 4) & 3;
	state->max_length = tag & 0xF;
	tag >>= 6;
	state->in++;

	switch (tag)
	{
		case TAG_LITS:
			error = decode_lits(state, size_in - 1, *size_out);
			break;
		case TAG_RLE:
			error = decode_rle(state, *size_out);
			break;
		case TAG_LENS:
			error = decode_lens(state, size_in - 1, *size_out);
			break;
		case TAG_CANON:
			error = decode_canon(state, size_in - 1, *size_out);
			break;
		default:
			error = EIO;
			break;
	}

	*size_out = state->out - buffer_out;
	return error;
}

unsigned int
hmz_decode_finish(const struct hmz_decode_state * const state)
{
	if (state != NULL)
		free((void *)state);

	return 0;
}
