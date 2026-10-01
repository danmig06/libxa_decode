// used to compile a (shared) object file out of xa_decode.h

#define XA_DECODE_IMPLEMENTATION
#include "xa_decode.h"

size_t xa_decode_available_samples(xa_decoder_t* xa);
xa_subhdr_t xa_get_subheader(xa_decoder_t* xa, const void* sector_data);
xa_sample16_t xa_decode_get_sample16(xa_decoder_t* xa);
xa_samplef_t xa_decode_get_samplef(xa_decoder_t* xa);

