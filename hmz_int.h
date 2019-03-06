#define MEM_ALIGN		64
#define SYMBOLS			256
#define MAX_CODE_LEN		12
#define TABLE_SIZE		(1 << MAX_CODE_LEN)
#define MIN_HEADER_SIZE		(1 + 1 + 4)
#define MAX_HEADER_SIZE		(1 + MAX_CODE_LEN + SYMBOLS + 20)
#define MEM_OVERRUN		8

#define TAG_LITS		0
#define TAG_RLE			1
#define TAG_LENS		2
#define TAG_CANON		3

struct encode_buf {
	unsigned long buf_val;
	unsigned int  buf_bits;
	unsigned char *buf_data;
};

struct decode_buf {
	unsigned long buf_val;
	unsigned int  buf_bits;
	const unsigned char *buf_data;
	const unsigned char *buf_end;
};

struct counts {
	unsigned int c[4][SYMBOLS];
};

struct symbol {
	unsigned int count;
	unsigned int symbol;
};

struct decode {
	unsigned char symbol[3];
	unsigned int  count:2;
	unsigned int  length:6;
};

struct hmz_encode_state {
	struct counts counts;
	struct symbol freqs[SYMBOLS];
	struct symbol base[SYMBOLS*2];
	struct symbol *nodes;
	unsigned int  lengths[SYMBOLS];
	unsigned long codes[SYMBOLS];
	unsigned int  code_counts[16];
	unsigned int  symbol_count;
	unsigned int  max_count;
	unsigned int  max_symbol;
	unsigned int  max_length;
	unsigned int  overflow;
	unsigned int  format;
	const unsigned char *in;
	unsigned char *out;
};

struct hmz_decode_state {
	struct symbol symbols[SYMBOLS];
	struct decode table[TABLE_SIZE];
	unsigned int  lengths[SYMBOLS];
	unsigned int  code_counts[16];
	unsigned int  next_index[16];
	unsigned int  symbol_count;
	unsigned int  max_length;
	unsigned int  format;
	const unsigned char *in;
	unsigned char *out;
};
