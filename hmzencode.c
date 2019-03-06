#include <sys/types.h>
#include <sys/errno.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "hmz_int.h"
#include "hmz.h"

static inline void
buf_encode_init(struct encode_buf * const buf, unsigned char * const data)
{
	buf->buf_val = 0;
	buf->buf_bits = 64;
	buf->buf_data = data;
}

static inline void
buf_encode_write(struct encode_buf * const buf)
{
	unsigned long bswap;
	unsigned int bytes;
	unsigned int bits;

	bytes = (64 - buf->buf_bits) >> 3;
	bits = bytes << 3;
	bswap = __builtin_bswap64(buf->buf_val);
	memcpy(buf->buf_data, &bswap, 8);
	buf->buf_data += bytes;
	buf->buf_bits += bits;
	buf->buf_val <<= bits;
}

static inline void
buf_encode_bits(struct encode_buf * const buf, const unsigned long code,
    const unsigned int bits)
{
	buf->buf_bits -= bits;
	buf->buf_val |= code << buf->buf_bits;
}

static inline unsigned char *
buf_encode_end(struct encode_buf * const buf)
{
	unsigned int bytes;
	unsigned long bswap;

	if (buf->buf_bits < 64) {
		bytes = (64 - buf->buf_bits + 7) >> 3;
		bswap = __builtin_bswap64(buf->buf_val);
		memcpy(buf->buf_data, &bswap, bytes);
		buf->buf_data += bytes;
	}
	*(buf->buf_data) = buf->buf_bits & 7;
	buf->buf_data++;

	return buf->buf_data;
}

static inline void
init_state(struct hmz_encode_state * const state,
    const unsigned char * const buffer_in,
    unsigned char * const buffer_out)
{
	state->in = buffer_in;
	state->out = buffer_out;
	state->symbol_count = 0;
	state->max_count = 0;
	state->overflow = 0;
}

static inline void
count_one(struct hmz_encode_state * const state, const unsigned int v)
{
	const unsigned char v1 = v;
	const unsigned char v2 = v >> 8;
	const unsigned char v3 = v >> 16;
	const unsigned char v4 = v >> 24;

	state->counts.c[0][v1]++;
	state->counts.c[1][v2]++;
	state->counts.c[2][v3]++;
	state->counts.c[3][v4]++;
}

static inline void
count_freqs(struct hmz_encode_state * const state,
    const unsigned char * const encode_buf, const unsigned int size)
{
	const unsigned char *curr = encode_buf;
	const unsigned char * const end = encode_buf + size;
	struct symbol *symp;
	unsigned int v;
	unsigned int n;
	unsigned int i;

	memset(&state->counts, 0, sizeof(state->counts));

	memcpy(&n, curr, 4);
	curr += 4;
	while (curr < (end - 15)) {
		v = n;
		memcpy(&n, curr, 4);
		curr += 4;
		count_one(state, v);
		v = n;
		memcpy(&n, curr, 4);
		curr += 4;
		count_one(state, v);
		v = n;
		memcpy(&n, curr, 4);
		curr += 4;
		count_one(state, v);
		v = n;
		memcpy(&n, curr, 4);
		curr += 4;
		count_one(state, v);
	}
	count_one(state, n);

	while (curr < end)
		state->counts.c[0][*curr++]++;

	for (i = 0; i < SYMBOLS; i++) {
		symp = &state->freqs[state->symbol_count];

		symp->symbol = i;
		symp->count =
		    state->counts.c[0][i] +
		    state->counts.c[1][i] +
		    state->counts.c[2][i] +
		    state->counts.c[3][i];

		state->max_count |= symp->count;

		state->symbol_count += (symp->count > 0);
	}

	state->max_symbol = state->freqs[state->symbol_count - 1].symbol;
}

static inline void
sort_symbols(struct hmz_encode_state * const state)
{
	unsigned short offset[32];
	unsigned short counts[32];
	unsigned char weights[SYMBOLS];
	unsigned int weight;
	unsigned int count;
	unsigned int i;
	unsigned int j;

	memset(counts, 0, sizeof(counts));

	for (i = 0; i < state->symbol_count; i++) {
		weight = __builtin_clz(state->freqs[i].count);
		weights[i] = weight;
		counts[weight]++;
	}

	offset[0] = 0;
	for (i = 1; i < 32; i++)
		offset[i] = offset[i - 1] + counts[i - 1];

	for (i = 1; i < 32; i++)
		counts[i] = offset[i];

	for (i = 0; i < state->symbol_count; i++) {
		weight = weights[i];
		count = state->freqs[i].count;
		j = counts[weight]++;
		while (j > offset[weight] &&
		    count > state->nodes[j - 1].count) {
			state->nodes[j] = state->nodes[j - 1];
			j--;
		}
		state->nodes[j].count = state->freqs[i].count;
		state->nodes[j].symbol = state->freqs[i].symbol;
	}
}

static inline void
create_tree(struct hmz_encode_state * const state)
{
	struct symbol *symp;
	struct symbol *sym1;
	struct symbol *sym2;
	struct symbol *nodep;
	struct symbol *treep;
	unsigned int node_index = SYMBOLS;
	unsigned int symbol_count = state->symbol_count;
	unsigned int count;
	unsigned int i;

	state->base[0].count = 0xFFFFFFFF;
	for (i = SYMBOLS + 1; i < (SYMBOLS * 2); i++)
		state->base[i].count = 0xFFFFFFFF;

	symp = &state->nodes[state->symbol_count - 1];
	nodep = &state->nodes[SYMBOLS];
	treep = &state->nodes[SYMBOLS];

	while (symbol_count > 1) {
		sym1 = symp->count <= nodep->count ? symp-- : nodep++;
		sym2 = symp->count <= nodep->count ? symp-- : nodep++;
		treep->count = sym1->count + sym2->count;
		sym1->count = node_index;
		sym2->count = node_index;
		treep++;
		node_index++;
		symbol_count--;
	}

	state->nodes[--node_index].count = 0;
	while (node_index > SYMBOLS) {
		symp = &state->nodes[--node_index];

		count = state->nodes[symp->count].count + 1;
		state->overflow += (count > MAX_CODE_LEN);
		symp->count = count;
	}

	memset(state->code_counts, 0, sizeof(state->code_counts));
	memset(state->lengths, 0, sizeof(state->lengths));

	for (i = 0; i < state->symbol_count; i++) {
		symp = &state->nodes[i];

		count = state->nodes[symp->count].count + 1;
		if (count > MAX_CODE_LEN) {
			count = MAX_CODE_LEN;
			state->overflow++;
		}
		symp->count = count;
		state->code_counts[count]++;
		state->lengths[symp->symbol] = count;
	}

	state->max_length = state->nodes[state->symbol_count - 1].count;
}

static inline void
limit_lengths(struct hmz_encode_state * const state)
{
	unsigned int i;
	unsigned int j;
	unsigned int k;
	unsigned int val;

	if (state->overflow == 0)
		return;

	state->max_length = MAX_CODE_LEN;

	while (state->overflow > 0) {
		i = MAX_CODE_LEN - 1;
		while (state->code_counts[i] == 0)
			i--;
		state->code_counts[i]--;
		state->code_counts[i + 1] += 2;
		state->code_counts[MAX_CODE_LEN]--;
		state->overflow -= 2;
	}

	k = 0;
	for (i = 1; i <= MAX_CODE_LEN; i++) {
		val = state->code_counts[i];
		for (j = 0; j < val; j++)
			state->lengths[state->nodes[k++].symbol] = i;
	}
}

static inline void
create_codes(struct hmz_encode_state * const state)
{
	unsigned int next_code[16];
	unsigned int i;

	next_code[0] = 0;
	next_code[1] = 0;
	for (i = 2; i <= state->max_length; i++)
		next_code[i] =
		    (next_code[i - 1] + state->code_counts[i - 1]) << 1;

	for (i = 0; i <= state->max_symbol; i++)
		state->codes[i] = next_code[state->lengths[i]]++;
}

static inline unsigned long
total_length(const struct hmz_encode_state * const state)
{
	unsigned long total_bits = 0;
	unsigned long total_bytes = MAX_HEADER_SIZE + MEM_OVERRUN;
	unsigned int i;

	for (i = 0; i < state->symbol_count; i++)
		total_bits += state->freqs[i].count *
		    state->lengths[state->freqs[i].symbol];

	total_bytes += ((total_bits + 7) >> 3);

	return total_bytes;
}

static inline void
encode_lits(struct hmz_encode_state * const state, const unsigned int size_in)
{
	unsigned char *out = state->out;

	*out++ = TAG_LITS << 6;
	memcpy(out, &size_in, 4);
	out += 4;
	memcpy(out, state->in, size_in);

	state->out = out + size_in;
}

static inline void
encode_rle(struct hmz_encode_state * const state, const unsigned int size_in)
{
	unsigned char *out = state->out;

	*out++ = TAG_RLE << 6;
	*out++ = *(state->in);
	memcpy(out, &size_in, 4);

	state->out += 6;
}

static inline void
encode_lens(struct hmz_encode_state * const state)
{
	unsigned char *out = state->out;
	unsigned int i;

	*out++ = (TAG_LENS << 6) | (state->format << 4) | state->max_length;
	*out++ = state->max_symbol;

	for (i = 0; i <= state->max_symbol; i += 2)
		*out++ = state->lengths[i] << 4 | state->lengths[i + 1];

	state->out = out;
}

static inline void
encode_canon(struct hmz_encode_state * const state)
{
	unsigned char *out = state->out;
	unsigned int next_index[16];
	unsigned int i;

	*out++ = (TAG_CANON << 6) | (state->format << 4) | state->max_length;

	for (i = 1; i <= state->max_length; i++)
		*out++ = state->code_counts[i];

	next_index[0] = state->symbol_count;
	next_index[1] = 0;
	for (i = 2; i <= state->max_length; i++)
		next_index[i] = next_index[i - 1] + state->code_counts[i - 1];

	for (i = 0; i <= state->max_symbol; i++)
		out[next_index[state->lengths[i]]++] = i;

	out += state->symbol_count;

	state->out = out;
}

static inline void
encode_table(struct hmz_encode_state * const state)
{
	unsigned int cost_lens;
	unsigned int cost_canon;

	cost_lens = 1 + 1 + ((state->max_symbol + 1) >> 1);
	cost_canon = 1 + state->max_length + state->symbol_count;

	if (cost_lens < cost_canon)
		encode_lens(state);
	else
		encode_canon(state);
}

static inline void
encode_bytes(const struct hmz_encode_state * const state,
    struct encode_buf * const buf, const unsigned char *curr)
{
	unsigned long code;
	unsigned int bits;
	unsigned int sym;

	sym = *curr++;
	bits = state->lengths[sym];
	code = state->codes[sym];

	sym = *curr++;
	bits += state->lengths[sym];
	code <<= state->lengths[sym];
	code |= state->codes[sym];

	sym = *curr++;
	bits += state->lengths[sym];
	code <<= state->lengths[sym];
	code |= state->codes[sym];

	sym = *curr++;
	bits += state->lengths[sym];
	code <<= state->lengths[sym];
	code |= state->codes[sym];

	buf_encode_bits(buf, code, bits);
	buf_encode_write(buf);
}

static inline void
encode_byte(const struct hmz_encode_state * const state,
    struct encode_buf * const buf, const unsigned char *curr)
{
	unsigned int sym;

	sym = *curr++;
	buf_encode_bits(buf, state->codes[sym], state->lengths[sym]);
}

static inline unsigned int
encode_data_part(struct hmz_encode_state * const state,
    const unsigned int size)
{
	const unsigned char *curr = state->in;
	const unsigned char *end = curr + size;
	unsigned char *out;
	struct encode_buf buf;
	unsigned int osize;

	buf_encode_init(&buf, state->out);

	while (curr < (end - 15)) {
		encode_bytes(state, &buf, curr);
		encode_bytes(state, &buf, curr+4);
		encode_bytes(state, &buf, curr+8);
		encode_bytes(state, &buf, curr+12);
		curr += 16;
	}

	while (curr < (end-3)) {
		encode_bytes(state, &buf, curr);
		curr += 4;
	}

	while (curr < end) {
		encode_byte(state, &buf, curr);
		curr++;
	}

	state->in = curr;
	out = buf_encode_end(&buf);
	osize = out - state->out;
	state->out = out;
	return osize;
}

static inline void
encode_data_single(struct hmz_encode_state * const state,
    const unsigned int size)
{
	unsigned char *size_out = state->out;
	unsigned int osize;

	state->out += sizeof(osize);

	osize = encode_data_part(state, size);

	memcpy(size_out, &osize, sizeof(osize));
}

static inline void
encode_data_multi(struct hmz_encode_state * const state,
    const unsigned int size)
{
	unsigned char * const sizes_out = state->out;
	unsigned int sizes[5];
	unsigned int part;

	part = (size + 3) >> 2;
	state->out += sizeof(sizes);

	sizes[0] = part;
	sizes[1] = encode_data_part(state, part);
	sizes[2] = encode_data_part(state, part);
	sizes[3] = encode_data_part(state, part);
	sizes[4] = encode_data_part(state, size - (part + part + part));

	memcpy(sizes_out, &sizes, sizeof(sizes));
}

static inline void
encode_data(struct hmz_encode_state * const state, const unsigned int size)
{
	if (state->format == HMZ_FMT_SINGLE)
		encode_data_single(state, size);
	else
		encode_data_multi(state, size);
}

/*
 * Estimate worst case size of compressed data.
 */
unsigned int
hmz_compressed_size(const unsigned int size)
{
	const unsigned int csize = size + MAX_HEADER_SIZE + MEM_OVERRUN;

	return (csize < size) ? size : csize;
}

unsigned int
hmz_encode_init(struct hmz_encode_state ** const state,
    const unsigned int format)
{
	int error;

	if (format != HMZ_FMT_SINGLE && format != HMZ_FMT_MULTI)
		return EINVAL;

	error = posix_memalign((void **)state, MEM_ALIGN, sizeof(**state));
	if (error != 0)
		return ENOMEM;

	(*state)->format = format;
	(*state)->nodes = &(*state)->base[1];

	return 0;
}

unsigned int
hmz_encode(struct hmz_encode_state * const state,
    const unsigned char * const buffer_in, const unsigned int size_in,
    unsigned char * const buffer_out, unsigned int * const size_out)
{
	if (state == NULL || buffer_in == NULL || size_in == 0 ||
	    buffer_out == NULL || *size_out < MIN_HEADER_SIZE)
		return EINVAL;

	init_state(state, buffer_in, buffer_out);

	count_freqs(state, buffer_in, size_in);

	if (state->symbol_count == 1) {
		encode_rle(state, size_in);
		goto out;
	}

	if (state->max_count <= (size_in >> 7)) {
		if (size_in > (*size_out + 1 + 4))
			return EOVERFLOW;
		encode_lits(state, size_in);
		goto out;
	}

	sort_symbols(state);
	create_tree(state);
	limit_lengths(state);

	if (*size_out < hmz_compressed_size(size_in)) {
		if (*size_out < total_length(state))
			return EOVERFLOW;
	}

	create_codes(state);
	encode_table(state);
	encode_data(state, size_in);

 out:
	if ((state->out - buffer_out) > *size_out)
		return EOVERFLOW;
	*size_out = state->out - buffer_out;
	return 0;
}

unsigned int
hmz_encode_finish(const struct hmz_encode_state * const state)
{
	if (state != NULL)
		free((void *)state);

	return 0;
}
