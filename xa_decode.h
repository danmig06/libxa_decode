#ifndef XA_DECODE_H
#define XA_DECODE_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define XA_MAX_OUTPUT_SAMPLES (((2016 * 2) * 7 * 2) / 6)
#define XA_MAX_OUTPUT_SIZE (XA_MAX_OUTPUT_SAMPLES * sizeof(int16_t))

#ifdef __cplusplus
extern "C" {
#endif

// XA-ADPCM Coding Info Fields
enum {
	XA_CI_SM       = (1 << 0), // Stereo/Mono     (0=mono, 1=stereo)
	XA_CI_FS       = (1 << 2), // Sample rate     (0=37800Hz, 1=18900Hz)
	XA_CI_8BITS    = (1 << 4), // Bits per sample (0=4bit, 1=8bit)
	XA_CI_EMPHASIS = (1 << 6)  // Emphasis Filter Enable (unused)
};

typedef struct xa_decoder {
	struct {
		uint8_t index;
		uint8_t counter;
		int16_t buf[32];
	} resample[2];
	int16_t hist[2][2];
	struct {
		uint32_t read_off;
		uint32_t write_off;
		int16_t* buf;
	} dec[2];
	bool is_buffer_owned;
	uint8_t coding_info;
} xa_decoder_t;

typedef struct xa_subhdr {
	uint8_t file;
	uint8_t channel;
	uint8_t submode;
	uint8_t coding_info;
} xa_subhdr_t;

typedef struct xa_sample16 {
	int16_t left, right;
} xa_sample16_t;

typedef struct xa_samplef {
	float left, right;
} xa_samplef_t;

void xa_decode_init(xa_decoder_t* xa, void* output_buf);
void xa_decode_uninit(xa_decoder_t* xa);
void xa_decode_sector(xa_decoder_t* xa, const void* sector_data);

static inline xa_subhdr_t xa_get_subheader(xa_decoder_t* xa, const void* sector_data) {
	const xa_subhdr_t* subheader = (const xa_subhdr_t*)((const uint8_t*)sector_data + 16);
	return *subheader;
}

#define XA_LEFT 0
#define XA_RIGHT 1
#define XA_MONO XA_LEFT

static inline size_t xa_decode_available_samples(xa_decoder_t* xa) {
	return xa->dec[XA_LEFT].write_off - xa->dec[XA_LEFT].read_off;
}

static inline xa_sample16_t xa_decode_get_sample16(xa_decoder_t* xa) {
	xa_sample16_t sample;
	if(xa->coding_info & XA_CI_SM) {
		sample.left = xa->dec[XA_LEFT].buf[xa->dec[XA_LEFT].read_off++];
		sample.right = xa->dec[XA_RIGHT].buf[xa->dec[XA_RIGHT].read_off++];
	} else {
		sample.left = sample.right = xa->dec[XA_MONO].buf[xa->dec[XA_MONO].read_off++];
	}
	return sample;
}

static inline xa_samplef_t xa_decode_get_samplef(xa_decoder_t* xa) {
	xa_samplef_t sample;
	if(xa->coding_info & XA_CI_SM) {
		sample.left = (float)xa->dec[XA_LEFT].buf[xa->dec[XA_LEFT].read_off++] / 32767.0f;
		sample.right = (float)xa->dec[XA_RIGHT].buf[xa->dec[XA_RIGHT].read_off++] / 32767.0f;
	} else {
		sample.left = sample.right = (float)xa->dec[XA_MONO].buf[xa->dec[XA_MONO].read_off++] / 32767.0f;
	}
	return sample;
}

#ifdef XA_DECODE_IMPLEMENTATION

#include <string.h>
#include <stdlib.h>

#include <stdio.h> // only used for debug messages, TODO: remove this

#define XA_SAT(n, l, u) (((n) < (l)) ? (l) : ((n) > (u)) ? (u) : (n))

static int16_t g_zigzag_tables[7][29] = {
	{
		 0x0000,  0x0000,  0x0000,  0x0000,
		 0x0000, -0x0002,  0x000A, -0x0022,
		 0x0041, -0x0054,  0x0034,  0x0009,
		-0x010A,  0x0400, -0x0A78,  0x234C,
		 0x6794, -0x1780,  0x0BCD, -0x0623,
		 0x0350, -0x016D,  0x006B,  0x000A,
		-0x0010,  0x0011, -0x0008,  0x0003,
		-0x0001
	},
	{
		 0x0000,  0x0000,  0x0000, -0x0002,
		 0x0000,  0x0003, -0x0013,  0x003C,
		-0x004B,  0x00A2, -0x00E3,  0x0132,
		-0x0043, -0x0267,  0x0C9D,  0x74BB,
		-0x11B4,  0x09B8, -0x05BF,  0x0372,
		-0x01A8,  0x00A6, -0x001B,  0x0005,
		 0x0006, -0x0008,  0x0003, -0x0001,
		 0x0000
	},
	{
		 0x0000,  0x0000, -0x0001,  0x0003,
		-0x0002, -0x0005,  0x001F, -0x004A,
		 0x00B3, -0x0192,  0x02B1, -0x039E,
		 0x04F8, -0x05A6,  0x7939, -0x05A6,
		 0x04F8, -0x039E,  0x02B1, -0x0192,
		 0x00B3, -0x004A,  0x001F, -0x0005,
		-0x0002,  0x0003, -0x0001,  0x0000,
		 0x0000
	},
	{
		 0x0000, -0x0001,  0x0003, -0x0008,
		 0x0006,  0x0005, -0x001B,  0x00A6,
		-0x01A8,  0x0372, -0x05BF,  0x09B8,
		-0x11B4,  0x74BB,  0x0C9D, -0x0267,
		-0x0043,  0x0132, -0x00E3,  0x00A2,
		-0x004B,  0x003C, -0x0013,  0x0003,
		 0x0000, -0x0002,  0x0000,  0x0000,
		 0x0000
	},
	{
		 0x0001,  0x0003, -0x0008,  0x0011,
		-0x0010,  0x000A,  0x006B, -0x016D,
		 0x0350, -0x0623,  0x0BCD, -0x1780,
		 0x6794,  0x234C, -0x0A78,  0x0400,
		-0x010A,  0x0009,  0x0034, -0x0054,
		 0x0041, -0x0022,  0x000A, -0x0001,
		 0x0000,  0x0001,  0x0000,  0x0000,
		 0x0000
	},
	{
		 0x0002, -0x0008,  0x0010, -0x0023,
		 0x002B,  0x001A, -0x00EB,  0x027B,
		-0x0548,  0x0AFA, -0x16FA,  0x53E0,
		 0x3C07, -0x1249,  0x080E, -0x0347,
		 0x015B, -0x0044, -0x0017,  0x0046,
		-0x0023,  0x0011, -0x0005,  0x0000,
		 0x0000,  0x0000,  0x0000,  0x0000,
		 0x0000
	},
	{
		-0x0005,  0x0011, -0x0023,  0x0046,
		-0x0017, -0x0044,  0x015B, -0x0347,
		 0x080E, -0x1249,  0x3C07,  0x53E0,
		-0x16FA,  0x0AFA, -0x0548,  0x027B,
		-0x00EB,  0x001A,  0x002B, -0x0023,
		 0x0010, -0x0008,  0x0002,  0x0000,
		 0x0000,  0x0000,  0x0000,  0x0000,
		 0x0000
	}
};

static int16_t g_zigzag_tables_hr[7][25] = {
	{
		 0x0000, -0x0005,  0x0011, -0x0023,  0x0046, -0x0017, -0x0044,  0x015b, -0x0347,  0x080e, -0x1249,  0x3c07,  0x53e0,
		-0x16fa,  0x0afa, -0x0548,  0x027b, -0x00eb,  0x001a,  0x002b, -0x0023,  0x0010, -0x0008,  0x0002,  0x0000
	}, {
		 0x0000, -0x0002,  0x000a, -0x0022,  0x0041, -0x0054,  0x0034,  0x0009, -0x010a,  0x0400, -0x0a78,  0x234c,  0x6794,
		-0x1780,  0x0bcd, -0x0623,  0x0350, -0x016d,  0x006b,  0x000a, -0x0010,  0x0011, -0x0008,  0x0003, -0x0001
	}, {
		-0x0002,  0x0000,  0x0003, -0x0013,  0x003c, -0x004b,  0x00a2, -0x00e3,  0x0132, -0x0043, -0x0267,  0x0c9d,  0x74bb,
		-0x11b4,  0x09b8, -0x05bf,  0x0372, -0x01a8,  0x00a6, -0x001b,  0x0005,  0x0006, -0x0008,  0x0003, -0x0001
	}, {
		-0x0001,  0x0003, -0x0002, -0x0005,  0x001f, -0x004a,  0x00b3, -0x0192,  0x02b1, -0x039e,  0x04f8, -0x05a6,  0x7939,
		-0x05a6,  0x04f8, -0x039e,  0x02b1, -0x0192,  0x00b3, -0x004a,  0x001f, -0x0005, -0x0002,  0x0003, -0x0001
	}, {
		-0x0001,  0x0003, -0x0008,  0x0006,  0x0005, -0x001b,  0x00a6, -0x01a8,  0x0372, -0x05bf,  0x09b8, -0x11b4,  0x74bb,
		 0x0c9d, -0x0267, -0x0043,  0x0132, -0x00e3,  0x00a2, -0x004b,  0x003c, -0x0013,  0x0003,  0x0000, -0x0002
	}, {
		-0x0001,  0x0003, -0x0008,  0x0011, -0x0010,  0x000a,  0x006b, -0x016d,  0x0350, -0x0623,  0x0bcd, -0x1780,  0x6794,
		 0x234c, -0x0a78,  0x0400, -0x010a,  0x0009,  0x0034, -0x0054,  0x0041, -0x0022,  0x000a, -0x0002,  0x0000
	}, {
		 0x0000,  0x0002, -0x0008,  0x0010, -0x0023,  0x002b,  0x001a, -0x00eb,  0x027b, -0x0548,  0x0afa, -0x16fa,  0x53e0,
		 0x3c07, -0x1249,  0x080e, -0x0347,  0x015b, -0x0044, -0x0017,  0x0046, -0x0023,  0x0011, -0x0005,  0x0000
	}
};

static int g_xa_fc_old[] = { 0, 60, 115, 98 };
static int g_xa_fc_older[] = { 0, 0, 52, 55 };

static int16_t zigzag_interp(xa_decoder_t* xa, int ch, int t) {
	int32_t sum = 0;
	int p = xa->resample[ch].index;
	for(int i = 0; i < 29; i++) {
		sum += (xa->resample[ch].buf[(p - i) & 0x1f] * g_zigzag_tables[t][i]) / 0x8000;
	}
	return XA_SAT(sum, -0x8000, 0x7fff);
}

static void xa_resample(xa_decoder_t* xa, int16_t sample, int ch) {
	xa->resample[ch].buf[xa->resample[ch].index++] = sample;
	if(xa->resample[ch].index == 32) {
		xa->resample[ch].index = 0;
	}
	
	xa->resample[ch].counter--;
	if(xa->resample[ch].counter == 0) {
		xa->resample[ch].counter = 6;
		for(int t = 0; t < 7; t++) {
			xa->dec[ch].buf[xa->dec[ch].write_off++] = zigzag_interp(xa, ch, t);
		}
	}
}

// Half-Rate upsampling works a little differently, just copying the same sample twice won't work well

// According to Mednafen and Duckstation:

static int16_t zigzag_interp_hr(xa_decoder_t* xa, int ch) {
	int32_t sum = 0;
	int p = xa->resample[ch].index;
	int t = xa->resample[ch].counter;
	for(int i = 0; i < 25; i++) {
		sum += xa->resample[ch].buf[(p + 32 - 25 + i) & 0x1f] * g_zigzag_tables_hr[t][i];
	}
	return XA_SAT(sum / 0x8000, -0x8000, 0x7fff);
}

static void xa_resample_hr(xa_decoder_t* xa, int16_t sample, int ch) {
	bool frame_processed = false;
	do {
		if(xa->resample[ch].counter >= 7) {
			xa->resample[ch].buf[xa->resample[ch].index] = sample;
			xa->resample[ch].index = (xa->resample[ch].index + 1) & 0x1f;
			xa->resample[ch].counter -= 7;
			frame_processed = true;
		}

		xa->dec[ch].buf[xa->dec[ch].write_off++] = zigzag_interp_hr(xa, ch);
		xa->resample[ch].counter += 3;
	} while(!frame_processed);
}

static void xa_adpcm_decode_block(xa_decoder_t* xa, const uint8_t* src, int blk, int nibble, int ch) {
	if(xa->coding_info & XA_CI_8BITS) {
		fprintf(stderr, "[XA]: 8bit sample data is not supported");
		memset(xa->dec[XA_LEFT].buf, 0, XA_MAX_OUTPUT_SIZE);
		return;
	}
	uint8_t hdr = src[4 + (blk * 2) + nibble];

	int shift = hdr & 0xf;
	if(shift > 12) {
		shift = 9;
	}
	int filter = (hdr >> 4) & 3;

	int16_t* hist = xa->hist[ch];
	int16_t raw = 0;
	int32_t sample = 0;
	int old_coef = g_xa_fc_old[filter];
	int older_coef = g_xa_fc_older[filter];
	int8_t cur_byte;
	for(int i = 0; i < 28; i++) {
		cur_byte = (src[16 + blk + (i * 4)] >> (nibble * 4)) & 0xf;
		raw = ((int8_t)(cur_byte << 4)) >> 4;

		sample = raw << (12 - shift);
		sample += ((old_coef * hist[0]) - (older_coef * hist[1]) + 32) / 64;

		hist[1] = hist[0];
		hist[0] = XA_SAT(sample, -0x8000, 0x7fff);
		if(xa->coding_info & XA_CI_FS) {
			xa_resample_hr(xa, hist[0], ch);
		} else {
			xa_resample(xa, hist[0], ch);
		}
	}
}

void xa_decode_init(xa_decoder_t* xa, void* output_buf) {
	memset(xa, 0, sizeof(*xa));
	if(!output_buf) {
		xa->dec[XA_LEFT].buf = (int16_t*)malloc(XA_MAX_OUTPUT_SIZE);
		xa->is_buffer_owned = true;
	} else {
		xa->dec[XA_LEFT].buf = (int16_t*)output_buf;
	}
	xa->dec[XA_RIGHT].buf = xa->dec[XA_LEFT].buf + (XA_MAX_OUTPUT_SAMPLES / 2);
	xa->resample[XA_LEFT].counter = 6;
	xa->resample[XA_RIGHT].counter = 6;
}

void xa_decode_sector(xa_decoder_t* xa, const void* sector_data) {
	const uint8_t* src = (const uint8_t*)sector_data;
	xa->coding_info = src[12 + 4 + 3];
	bool is_stereo = (xa->coding_info & XA_CI_SM) != 0;
	xa->dec[XA_LEFT].read_off = 0;
	xa->dec[XA_LEFT].write_off = 0;
	xa->dec[XA_RIGHT].read_off = 0;
	xa->dec[XA_RIGHT].write_off = 0;
	src += 12 + 4 + 8;
	for(int i = 0; i < 18; i++) {
		for(int blk = 0; blk < 4; blk++) {
			if(is_stereo) {
				xa_adpcm_decode_block(xa, src, blk, 0, XA_LEFT);
				xa_adpcm_decode_block(xa, src, blk, 1, XA_RIGHT);
			} else {
				xa_adpcm_decode_block(xa, src, blk, 0, XA_MONO);
				xa_adpcm_decode_block(xa, src, blk, 1, XA_MONO);
			}
		}
		src += 128;
	}
}

void xa_decode_uninit(xa_decoder_t* xa) {
	if(xa->is_buffer_owned) {
		free(xa->dec[XA_LEFT].buf);
	}
}

#undef XA_SAT

#endif // #ifdef XA_DECODE_IMPLEMENTATION

#undef XA_LEFT
#undef XA_RIGHT
#undef XA_MONO

#ifdef __cplusplus
};
#endif

#endif // #ifndef XA_DECODE_H

