#ifdef __cplusplus
extern "C" {
#endif

#define SUFFIX		".hmz"
#define HEADER_VALUE	0x315A4D48

#define HMZ_FMT_SINGLE	0
#define HMZ_FMT_MULTI	1

#define HMZ_DEF_CHUNK	(1<<15)
#define HMZ_MAX_CHUNK	(1<<30)

struct hmz_encode_state;
struct hmz_decode_state;

unsigned int hmz_compressed_size(
    const unsigned int);

unsigned int hmz_encode_init(
    struct hmz_encode_state ** const state,
    const unsigned int format);

unsigned int hmz_encode(
    struct hmz_encode_state * const state,
    const unsigned char * const buffer_in,
    const unsigned int size_in,
    unsigned char * const buffer_out,
    unsigned int * const size_out);

unsigned int hmz_encode_finish(
    const struct hmz_encode_state * const state);

unsigned int hmz_decode_init(
    struct hmz_decode_state ** const state);

unsigned int hmz_decode(
    struct hmz_decode_state * const state,
    const unsigned char * const buffer_in,
    const unsigned int size_in,
    unsigned char * const buffer_out,
    unsigned int * const size_out);

unsigned int hmz_decode_finish(
    const struct hmz_decode_state * const state);

#ifdef __cplusplus
}
#endif
