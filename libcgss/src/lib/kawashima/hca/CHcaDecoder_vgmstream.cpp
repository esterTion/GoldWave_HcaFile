/**
 * clHCA DECODER
 *
 * Decodes CRI's HCA (High Compression Audio), a CBR DCT-based codec (similar to AAC).
 * Also supports what CRI calls HCA-MX, which basically is the same thing with constrained
 * encoder settings.
 *
 * - Original decompilation and C++ decoder by nyaga
 *     https://github.com/Nyagamon/HCADecoder
 * - Ported to C by kode54
 *     https://gist.github.com/kode54/ce2bf799b445002e125f06ed833903c0
 * - Cleaned up and re-reverse engineered for HCA v3 by bnnm, using Thealexbarney's VGAudio decoder as reference
 *     https://github.com/Thealexbarney/VGAudio
 */

 /* TODO:
  * - improve portability on types and float casts, sizeof(int) isn't necessarily sizeof(float)
  * - simplify DCT4 code
  * - add extra validations: encoder_delay/padding < sample_count, etc
  * - intensity should memset if intensity is 15 or set in reset? (no games hit 15?)
  * - check mdct + tables, add floats
  * - simplify bitreader to use in decoder only (no need to read +16 bits)
  */

  //--------------------------------------------------
  // Includes
  //--------------------------------------------------
#include "CHcaDecoder.h"
#include "CHcaDecoder_vgmstream.h"
#include <stddef.h>
#include <stdlib.h>
#include <memory.h>


/* CRI libs may only accept last version in some cases/modes, though most decoding takes older versions
 * into account. Lib is identified with "HCA Decoder (Float)" + version string. Some known versions:
 * - ~V1.1 2011 [first public version]
 * - ~V1.2 2011 [ciph/ath chunks, disabled ATH]
 * - Ver.1.40 2011-04 [header mask]
 * - Ver.1.42 2011-05
 * - Ver.1.45.02 2012-03
 * - Ver.2.00.02 2012-06 [decoding updates]
 * - Ver.2.02.02 2013-12, 2014-11
 * - Ver.2.06.05 2018-12 [scramble subkey API]
 * - Ver.2.06.07 2020-02, 2021-02
 * - Ver.3.01.00 2020-11 [decoding updates]
 * Same version rebuilt gets a newer date, and new APIs change header strings, but header versions
 * only change when decoder does. Despite the name, no "Integer" version seems to exist.
 */
#define HCA_VERSION_V101 0x0101 /* V1.1+ [El Shaddai (PS3/X360)] */
#define HCA_VERSION_V102 0x0102 /* V1.2+ [Gekka Ryouran Romance (PSP)] */
#define HCA_VERSION_V103 0x0103 /* V1.4+ [Phantasy Star Online 2 (PC), Binary Domain (PS3)] */
#define HCA_VERSION_V200 0x0200 /* V2.0+ [Yakuza 5 (PS3)] */
#define HCA_VERSION_V300 0x0300 /* V3.0+ [Uma Musume (Android), Megaton Musashi (Switch)-sfx-hfrgroups] */

 /* maxs depend on encoder quality settings (for example, stereo has:
  * highest=0x400, high=0x2AA, medium=0x200, low=0x155, lowest=0x100) */
#define HCA_MIN_FRAME_SIZE 0x8          /* lib min */
#define HCA_MAX_FRAME_SIZE 0xFFFF       /* lib max */

#define HCA_MASK  0x7F7F7F7F            /* chunk obfuscation when the HCA is encrypted with key */
#define HCA_SUBFRAMES  8
#define HCA_SAMPLES_PER_SUBFRAME  128   /* also spectrum points/etc */
#define HCA_SAMPLES_PER_FRAME  (HCA_SUBFRAMES*HCA_SAMPLES_PER_SUBFRAME)
#define HCA_MDCT_BITS  7                /* (1<<7) = 128 */

#define HCA_MIN_CHANNELS  1
#define HCA_MAX_CHANNELS  16            /* internal max (in practice only 8 can be encoded) */
#define HCA_MIN_SAMPLE_RATE  1          /* assumed */
#define HCA_MAX_SAMPLE_RATE  0x7FFFFF   /* encoder max seems 48000 */

#define HCA_DEFAULT_RANDOM  1

#define HCA_RESULT_OK            0
#define HCA_ERROR_PARAMS        -1
#define HCA_ERROR_HEADER        -2
#define HCA_ERROR_CHECKSUM      -3
#define HCA_ERROR_SYNC          -4
#define HCA_ERROR_UNPACK        -5
#define HCA_ERROR_BITREADER     -6

  //--------------------------------------------------
  // Decoder config/state
  //--------------------------------------------------
//typedef enum { DISCRETE = 0, STEREO_PRIMARY = 1, STEREO_SECONDARY = 2 } channel_type_t;

//--------------------------------------------------
// Bitstream reader
//--------------------------------------------------
void bitreader_init(clData* br, const void* data, int size) {
    br->data = (const unsigned char*)data;
    br->size = size * 8;
    br->bit = 0;
}

/* CRI's bitreader only handles 16b max during decode (header just reads bytes)
 * so maybe could be optimized by ignoring higher cases */
static unsigned int bitreader_peek(clData* br, int bitsize) {
    const unsigned int bit = br->bit;
    const unsigned int bit_rem = bit & 7;
    const unsigned int size = br->size;
    unsigned int v = 0;
    unsigned int bit_offset, bit_left;

    if (!(bit + bitsize <= size))
        return v;

    bit_offset = bitsize + bit_rem;
    bit_left = size - bit;
    if (bit_left >= 32 && bit_offset >= 25) {
        static const unsigned int mask[8] = {
                0xFFFFFFFF,0x7FFFFFFF,0x3FFFFFFF,0x1FFFFFFF,
                0x0FFFFFFF,0x07FFFFFF,0x03FFFFFF,0x01FFFFFF
        };
        const unsigned char* data = &br->data[bit >> 3];
        v = data[0];
        v = (v << 8) | data[1];
        v = (v << 8) | data[2];
        v = (v << 8) | data[3];
        v &= mask[bit_rem];
        v >>= 32 - bit_rem - bitsize;
    }
    else if (bit_left >= 24 && bit_offset >= 17) {
        static const unsigned int mask[8] = {
                0xFFFFFF,0x7FFFFF,0x3FFFFF,0x1FFFFF,
                0x0FFFFF,0x07FFFF,0x03FFFF,0x01FFFF
        };
        const unsigned char* data = &br->data[bit >> 3];
        v = data[0];
        v = (v << 8) | data[1];
        v = (v << 8) | data[2];
        v &= mask[bit_rem];
        v >>= 24 - bit_rem - bitsize;
    }
    else if (bit_left >= 16 && bit_offset >= 9) {
        static const unsigned int mask[8] = {
                0xFFFF,0x7FFF,0x3FFF,0x1FFF,0x0FFF,0x07FF,0x03FF,0x01FF
        };
        const unsigned char* data = &br->data[bit >> 3];
        v = data[0];
        v = (v << 8) | data[1];
        v &= mask[bit_rem];
        v >>= 16 - bit_rem - bitsize;
    }
    else {
        static const unsigned int mask[8] = {
                0xFF,0x7F,0x3F,0x1F,0x0F,0x07,0x03,0x01
        };
        const unsigned char* data = &br->data[bit >> 3];
        v = data[0];
        v &= mask[bit_rem];
        v >>= 8 - bit_rem - bitsize;
    }
    return v;
}

unsigned int bitreader_read(clData* br, int bitsize) {
    unsigned int v = bitreader_peek(br, bitsize);
    br->bit += bitsize;
    return v;
}

static void bitreader_skip(clData* br, int bitsize) {
    br->bit += bitsize;
}



//--------------------------------------------------
// ATH
//--------------------------------------------------
/* Base ATH (Absolute Threshold of Hearing) curve (for 41856hz).
 * May be a slight modification of the standard Painter & Spanias ATH curve formula. */
static const unsigned char ath_base_curve[656] = {
    0x78,0x5F,0x56,0x51,0x4E,0x4C,0x4B,0x49,0x48,0x48,0x47,0x46,0x46,0x45,0x45,0x45,
    0x44,0x44,0x44,0x44,0x43,0x43,0x43,0x43,0x43,0x43,0x42,0x42,0x42,0x42,0x42,0x42,
    0x42,0x42,0x41,0x41,0x41,0x41,0x41,0x41,0x41,0x41,0x41,0x41,0x40,0x40,0x40,0x40,
    0x40,0x40,0x40,0x40,0x40,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,
    0x3F,0x3F,0x3F,0x3E,0x3E,0x3E,0x3E,0x3E,0x3E,0x3D,0x3D,0x3D,0x3D,0x3D,0x3D,0x3D,
    0x3C,0x3C,0x3C,0x3C,0x3C,0x3C,0x3C,0x3C,0x3B,0x3B,0x3B,0x3B,0x3B,0x3B,0x3B,0x3B,
    0x3B,0x3B,0x3B,0x3B,0x3B,0x3B,0x3B,0x3B,0x3B,0x3B,0x3B,0x3B,0x3B,0x3B,0x3B,0x3B,
    0x3B,0x3B,0x3B,0x3B,0x3B,0x3B,0x3B,0x3B,0x3C,0x3C,0x3C,0x3C,0x3C,0x3C,0x3C,0x3C,
    0x3D,0x3D,0x3D,0x3D,0x3D,0x3D,0x3D,0x3D,0x3E,0x3E,0x3E,0x3E,0x3E,0x3E,0x3E,0x3F,
    0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,
    0x3F,0x3F,0x3F,0x3F,0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,
    0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x41,0x41,0x41,0x41,0x41,0x41,0x41,
    0x41,0x41,0x41,0x41,0x41,0x41,0x41,0x41,0x41,0x41,0x41,0x41,0x41,0x41,0x41,0x41,
    0x41,0x41,0x41,0x41,0x41,0x41,0x41,0x42,0x42,0x42,0x42,0x42,0x42,0x42,0x42,0x42,
    0x42,0x42,0x42,0x42,0x42,0x42,0x42,0x42,0x42,0x42,0x42,0x42,0x42,0x43,0x43,0x43,
    0x43,0x43,0x43,0x43,0x43,0x43,0x43,0x43,0x43,0x43,0x43,0x43,0x43,0x43,0x44,0x44,
    0x44,0x44,0x44,0x44,0x44,0x44,0x44,0x44,0x44,0x44,0x44,0x44,0x45,0x45,0x45,0x45,
    0x45,0x45,0x45,0x45,0x45,0x45,0x45,0x45,0x46,0x46,0x46,0x46,0x46,0x46,0x46,0x46,
    0x46,0x46,0x47,0x47,0x47,0x47,0x47,0x47,0x47,0x47,0x47,0x47,0x48,0x48,0x48,0x48,
    0x48,0x48,0x48,0x48,0x49,0x49,0x49,0x49,0x49,0x49,0x49,0x49,0x4A,0x4A,0x4A,0x4A,
    0x4A,0x4A,0x4A,0x4A,0x4B,0x4B,0x4B,0x4B,0x4B,0x4B,0x4B,0x4C,0x4C,0x4C,0x4C,0x4C,
    0x4C,0x4D,0x4D,0x4D,0x4D,0x4D,0x4D,0x4E,0x4E,0x4E,0x4E,0x4E,0x4E,0x4F,0x4F,0x4F,
    0x4F,0x4F,0x4F,0x50,0x50,0x50,0x50,0x50,0x51,0x51,0x51,0x51,0x51,0x52,0x52,0x52,
    0x52,0x52,0x53,0x53,0x53,0x53,0x54,0x54,0x54,0x54,0x54,0x55,0x55,0x55,0x55,0x56,
    0x56,0x56,0x56,0x57,0x57,0x57,0x57,0x57,0x58,0x58,0x58,0x59,0x59,0x59,0x59,0x5A,
    0x5A,0x5A,0x5A,0x5B,0x5B,0x5B,0x5B,0x5C,0x5C,0x5C,0x5D,0x5D,0x5D,0x5D,0x5E,0x5E,
    0x5E,0x5F,0x5F,0x5F,0x60,0x60,0x60,0x61,0x61,0x61,0x61,0x62,0x62,0x62,0x63,0x63,
    0x63,0x64,0x64,0x64,0x65,0x65,0x66,0x66,0x66,0x67,0x67,0x67,0x68,0x68,0x68,0x69,
    0x69,0x6A,0x6A,0x6A,0x6B,0x6B,0x6B,0x6C,0x6C,0x6D,0x6D,0x6D,0x6E,0x6E,0x6F,0x6F,
    0x70,0x70,0x70,0x71,0x71,0x72,0x72,0x73,0x73,0x73,0x74,0x74,0x75,0x75,0x76,0x76,
    0x77,0x77,0x78,0x78,0x78,0x79,0x79,0x7A,0x7A,0x7B,0x7B,0x7C,0x7C,0x7D,0x7D,0x7E,
    0x7E,0x7F,0x7F,0x80,0x80,0x81,0x81,0x82,0x83,0x83,0x84,0x84,0x85,0x85,0x86,0x86,
    0x87,0x88,0x88,0x89,0x89,0x8A,0x8A,0x8B,0x8C,0x8C,0x8D,0x8D,0x8E,0x8F,0x8F,0x90,
    0x90,0x91,0x92,0x92,0x93,0x94,0x94,0x95,0x95,0x96,0x97,0x97,0x98,0x99,0x99,0x9A,
    0x9B,0x9B,0x9C,0x9D,0x9D,0x9E,0x9F,0xA0,0xA0,0xA1,0xA2,0xA2,0xA3,0xA4,0xA5,0xA5,
    0xA6,0xA7,0xA7,0xA8,0xA9,0xAA,0xAA,0xAB,0xAC,0xAD,0xAE,0xAE,0xAF,0xB0,0xB1,0xB1,
    0xB2,0xB3,0xB4,0xB5,0xB6,0xB6,0xB7,0xB8,0xB9,0xBA,0xBA,0xBB,0xBC,0xBD,0xBE,0xBF,
    0xC0,0xC1,0xC1,0xC2,0xC3,0xC4,0xC5,0xC6,0xC7,0xC8,0xC9,0xC9,0xCA,0xCB,0xCC,0xCD,
    0xCE,0xCF,0xD0,0xD1,0xD2,0xD3,0xD4,0xD5,0xD6,0xD7,0xD8,0xD9,0xDA,0xDB,0xDC,0xDD,
    0xDE,0xDF,0xE0,0xE1,0xE2,0xE3,0xE4,0xE5,0xE6,0xE7,0xE8,0xE9,0xEA,0xEB,0xED,0xEE,
    0xEF,0xF0,0xF1,0xF2,0xF3,0xF4,0xF5,0xF7,0xF8,0xF9,0xFA,0xFB,0xFC,0xFD,0xFF,0xFF,
};

static void ath_init0(unsigned char* ath_curve) {
    /* disable curve */
    memset(ath_curve, 0, sizeof(ath_curve[0]) * HCA_SAMPLES_PER_SUBFRAME);
}

static void ath_init1(unsigned char* ath_curve, unsigned int sample_rate) {
    unsigned int i, index;
    unsigned int acc = 0;

    /* scale ATH curve depending on frequency */
    for (i = 0; i < HCA_SAMPLES_PER_SUBFRAME; i++) {
        acc += sample_rate;
        index = acc >> 13;

        if (index >= 654) {
            memset(ath_curve + i, 0xFF, sizeof(ath_curve[0]) * (HCA_SAMPLES_PER_SUBFRAME - i));
            break;
        }
        ath_curve[i] = ath_base_curve[index];
    }
}

static int ath_init(unsigned char* ath_curve, int type, unsigned int sample_rate) {
    switch (type) {
    case 0:
        ath_init0(ath_curve);
        break;
    case 1:
        ath_init1(ath_curve, sample_rate);
        break;
    default:
        return HCA_ERROR_HEADER;
    }
    return HCA_RESULT_OK;
}


//--------------------------------------------------
// Decode
//--------------------------------------------------
int unpack_scalefactors(stChannel* ch, clData* br, unsigned int hfr_group_count, unsigned int version);

int unpack_intensity(stChannel* ch, clData* br, unsigned int hfr_group_count, unsigned int version);

void calculate_resolution(stChannel* ch, unsigned int packed_noise_level, const unsigned char* ath_curve,
    unsigned int min_resolution, unsigned int max_resolution);

void calculate_gain(stChannel* ch);

void dequantize_coefficients(stChannel* ch, clData* br, int subframe);

void reconstruct_noise(stChannel* ch, unsigned int min_resolution, unsigned int ms_stereo, unsigned int* random_p, int subframe);

void reconstruct_high_frequency(stChannel* ch, unsigned int hfr_group_count, unsigned int bands_per_hfr_group,
    unsigned int stereo_band_count, unsigned int base_band_count, unsigned int total_band_count, unsigned int version, int subframe);

void apply_intensity_stereo(stChannel* ch_pair, int subframe, unsigned int base_band_count, unsigned int total_band_count);

void apply_ms_stereo(stChannel* ch_pair, unsigned int ms_stereo, unsigned int base_band_count, unsigned int total_band_count, int subframe);

void imdct_transform(stChannel* ch, int subframe);

//--------------------------------------------------
// Decode 1st step
//--------------------------------------------------
/* curve/scale to quantized resolution */
static const unsigned char hcadecoder_invert_table[66] = {
    14,14,14,14,14,14,13,13, 13,13,13,13,12,12,12,12,
    12,12,11,11,11,11,11,11, 10,10,10,10,10,10,10, 9,
     9, 9, 9, 9, 9, 8, 8, 8,  8, 8, 8, 7, 6, 6, 5, 4,
     4, 4, 3, 3, 3, 2, 2, 2,  2, 1, 1, 1, 1, 1, 1, 1,
     1, 1,
     /* indexes after 56 are not defined in v2.0<= (manually clamped to 1) */
};

/* scalefactor-to-scaling table, generated from sqrt(128) * (2^(53/128))^(scale_factor - 63) */
static const unsigned int hcadequantizer_scaling_table_float_hex[64] = {
    0x342A8D26,0x34633F89,0x3497657D,0x34C9B9BE,0x35066491,0x353311C4,0x356E9910,0x359EF532,
    0x35D3CCF1,0x360D1ADF,0x363C034A,0x367A83B3,0x36A6E595,0x36DE60F5,0x371426FF,0x3745672A,
    0x37838359,0x37AF3B79,0x37E97C38,0x381B8D3A,0x384F4319,0x388A14D5,0x38B7FBF0,0x38F5257D,
    0x3923520F,0x39599D16,0x3990FA4D,0x39C12C4D,0x3A00B1ED,0x3A2B7A3A,0x3A647B6D,0x3A9837F0,
    0x3ACAD226,0x3B071F62,0x3B340AAF,0x3B6FE4BA,0x3B9FD228,0x3BD4F35B,0x3C0DDF04,0x3C3D08A4,
    0x3C7BDFED,0x3CA7CD94,0x3CDF9613,0x3D14F4F0,0x3D467991,0x3D843A29,0x3DB02F0E,0x3DEAC0C7,
    0x3E1C6573,0x3E506334,0x3E8AD4C6,0x3EB8FBAF,0x3EF67A41,0x3F243516,0x3F5ACB94,0x3F91C3D3,
    0x3FC238D2,0x400164D2,0x402C6897,0x4065B907,0x40990B88,0x40CBEC15,0x4107DB35,0x413504F3,
};
static const float* hcadequantizer_scaling_table_float = (const float*)hcadequantizer_scaling_table_float_hex;

/* in v2.0 lib index 0 is 0x00000000, but resolution 0 is only valid in v3.0 files */
static const unsigned int hcadequantizer_range_table_float_hex[16] = {
    0x3F800000,0x3F2AAAAB,0x3ECCCCCD,0x3E924925,0x3E638E39,0x3E3A2E8C,0x3E1D89D9,0x3E088889,
    0x3D842108,0x3D020821,0x3C810204,0x3C008081,0x3B804020,0x3B002008,0x3A801002,0x3A000801,
};
static const float* hcadequantizer_range_table_float = (const float*)hcadequantizer_range_table_float_hex;

/* get scale indexes to normalize dequantized coefficients */
int unpack_scalefactors(stChannel* ch, clData* br, unsigned int hfr_group_count, unsigned int version) {
    int i;
    unsigned int cs_count = ch->coded_count;
    unsigned int extra_count;
    unsigned char delta_bits = bitreader_read(br, 3);

    /* added in v3.0 */
    if (ch->type == STEREO_SECONDARY || hfr_group_count <= 0 || version <= HCA_VERSION_V200) {
        extra_count = 0;
    }
    else {
        extra_count = hfr_group_count;
        cs_count = cs_count + extra_count;

        /* just in case */
        if (cs_count > HCA_SAMPLES_PER_SUBFRAME)
            return HCA_ERROR_UNPACK;
    }

    /* lib does check that cs_count is 2+ in fixed/delta case, but doesn't seem to affect anything */
    if (delta_bits >= 6) {
        /* fixed scalefactors */
        for (i = 0; i < cs_count; i++) {
            ch->scalefactors[i] = bitreader_read(br, 6);
        }
    }
    else if (delta_bits > 0) {
        /* delta scalefactors */
        const unsigned char expected_delta = (1 << delta_bits) - 1;
        unsigned char value = bitreader_read(br, 6);

        ch->scalefactors[0] = value;
        for (i = 1; i < cs_count; i++) {
            unsigned char delta = bitreader_read(br, delta_bits);

            if (delta == expected_delta) {
                value = bitreader_read(br, 6); /* encoded */
            }
            else {
                /* may happen with bad keycodes, scalefactors must be 6b indexes */
                int scalefactor_test = (int)value + ((int)delta - (int)(expected_delta >> 1));
                if (scalefactor_test < 0 || scalefactor_test >= 64) {
                    return HCA_ERROR_UNPACK;
                }

                value = value - (expected_delta >> 1) + delta; /* differential */
                value = value & 0x3F; /* v3.0 lib */

                //todo as negative better? (may roll otherwise?)
                //if (value >= 64)
                //    return HCA_ERROR_UNPACK;
            }
            ch->scalefactors[i] = value;
        }
    }
    else {
        /* no scalefactors */
        for (i = 0; i < HCA_SAMPLES_PER_SUBFRAME; i++) {
            ch->scalefactors[i] = 0;
        }
    }

    /* set derived HFR scales for v3.0 */
    for (i = 0; i < extra_count; i++) {
        ch->scalefactors[HCA_SAMPLES_PER_SUBFRAME - 1 - i] = ch->scalefactors[cs_count - i];
    }

    return HCA_RESULT_OK;
}

/* read intensity (for joint stereo R) or v2.0 high frequency scales (for regular channels) */
int unpack_intensity(stChannel* ch, clData* br, unsigned int hfr_group_count, unsigned int version) {
    int i;

    if (ch->type == STEREO_SECONDARY) {
        /* read subframe intensity for channel pair (peek first for valid values, not sure why not consumed) */
        if (version <= HCA_VERSION_V200) {
            unsigned char value = bitreader_peek(br, 4);

            ch->intensity[0] = value;
            if (value < 15) {
                bitreader_skip(br, 4);
                for (i = 1; i < HCA_SUBFRAMES; i++) {
                    ch->intensity[i] = bitreader_read(br, 4);
                }
            }
            /* 15 may be an invalid value? index 15 is 0, but may imply "reuse last subframe's intensity".
             * no games seem to use 15 though */
             //else {
             //    return HCA_ERROR_UNPACK;
             //}
        }
        else {
            unsigned char value = bitreader_peek(br, 4);
            unsigned char delta_bits;

            if (value < 15) {
                bitreader_skip(br, 4);

                delta_bits = bitreader_read(br, 2); /* +1 */

                ch->intensity[0] = value;
                if (delta_bits == 3) { /* 3+1 = 4b */
                    /* fixed intensities */
                    for (i = 1; i < HCA_SUBFRAMES; i++) {
                        ch->intensity[i] = bitreader_read(br, 4);
                    }
                }
                else {
                    /* delta intensities */
                    unsigned char bmax = (2 << delta_bits) - 1;
                    unsigned char bits = delta_bits + 1;

                    for (i = 1; i < HCA_SUBFRAMES; i++) {
                        unsigned char delta = bitreader_read(br, bits);
                        if (delta == bmax) {
                            value = bitreader_read(br, 4); /* encoded */
                        }
                        else {
                            value = value - (bmax >> 1) + delta; /* differential */
                            if (value > 15) //todo check
                                return HCA_ERROR_UNPACK; /* not done in lib */
                        }

                        ch->intensity[i] = value;
                    }
                }
            }
            else {
                bitreader_skip(br, 4);
                for (i = 0; i < HCA_SUBFRAMES; i++) {
                    ch->intensity[i] = 7;
                }
            }
        }
    }
    else {
        /* read high frequency scalefactors (v3.0 uses derived values in unpack_scalefactors instead) */
        if (version <= HCA_VERSION_V200) {
            /* pointer in v2.0 lib for v2.0 files is base+stereo bands, while v3.0 lib for v2.0 files
             * is last HFR. No output difference but v3.0 files need that to handle HFR */
             //unsigned char* hfr_scales = &ch->scalefactors[base_band_count + stereo_band_count]; /* v2.0 lib */
            unsigned char* hfr_scales = &ch->scalefactors[128 - hfr_group_count]; /* v3.0 lib */

            for (i = 0; i < hfr_group_count; i++) {
                hfr_scales[i] = bitreader_read(br, 6);
            }
        }
    }

    return HCA_RESULT_OK;
}

/* get resolutions, that determines range of values per encoded spectrum coefficients */
void calculate_resolution(stChannel* ch, unsigned int packed_noise_level, const unsigned char* ath_curve, unsigned int min_resolution, unsigned int max_resolution) {
    int i;
    unsigned int cr_count = ch->coded_count;
    unsigned int noise_count = 0;
    unsigned int valid_count = 0;

    for (i = 0; i < cr_count; i++) {
        unsigned char new_resolution = 0;
        unsigned char scalefactor = ch->scalefactors[i];

        if (scalefactor > 0) {
            /* curve values are 0 in v1.2>= so ath_curve is actually removed in CRI's code */
            int noise_level = ath_curve[i] + ((packed_noise_level + i) >> 8);
            int curve_position = noise_level + 1 - ((5 * scalefactor) >> 1);

            /* v2.0<= allows max 56 + sets rest to 1, while v3.0 table has 1 for 57..65 and
             * clamps to min_resolution below, so v2.0 files are still supported */
            if (curve_position < 0) {
                new_resolution = 15;
            }
            else if (curve_position <= 65) {
                new_resolution = hcadecoder_invert_table[curve_position];
            }
            else {
                new_resolution = 0;
            }

            /* added in v3.0 (before, min_resolution was always 1) */
            if (new_resolution > max_resolution)
                new_resolution = max_resolution;
            else if (new_resolution < min_resolution)
                new_resolution = min_resolution;

            /* save resolution 0 (not encoded) indexes (from 0..N), and regular indexes (from N..0) */
            if (new_resolution < 1) {
                ch->noises[noise_count] = i;
                noise_count++;
            }
            else {
                ch->noises[HCA_SAMPLES_PER_SUBFRAME - 1 - valid_count] = i;
                valid_count++;
            }
        }
        ch->resolution[i] = new_resolution;
    }

    ch->noise_count = noise_count;
    ch->valid_count = valid_count;

    memset(&ch->resolution[cr_count], 0, sizeof(ch->resolution[0]) * (HCA_SAMPLES_PER_SUBFRAME - cr_count));
}

/* get actual scales to dequantize based on saved scalefactors */
// HCADequantizer_CalculateGain
void calculate_gain(stChannel* ch) {
    int i;
    unsigned int cg_count = ch->coded_count;

    for (i = 0; i < cg_count; i++) {
        float scalefactor_scale = hcadequantizer_scaling_table_float[ch->scalefactors[i]];
        float resolution_scale = hcadequantizer_range_table_float[ch->resolution[i]];
        ch->gain[i] = scalefactor_scale * resolution_scale;
    }
}

//--------------------------------------------------
// Decode 2nd step
//--------------------------------------------------
/* coded resolution to max bits */
static const unsigned char hcatbdecoder_max_bit_table[16] = {
    0,2,3,3,4,4,4,4, 5,6,7,8,9,10,11,12
};
/* bits used for quant codes */
static const unsigned char hcatbdecoder_read_bit_table[128] = {
    0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
    1,1,2,2,0,0,0,0, 0,0,0,0,0,0,0,0,
    2,2,2,2,2,2,3,3, 0,0,0,0,0,0,0,0,
    2,2,3,3,3,3,3,3, 0,0,0,0,0,0,0,0,
    3,3,3,3,3,3,3,3, 3,3,3,3,3,3,4,4,
    3,3,3,3,3,3,3,3, 3,3,4,4,4,4,4,4,
    3,3,3,3,3,3,4,4, 4,4,4,4,4,4,4,4,
    3,3,4,4,4,4,4,4, 4,4,4,4,4,4,4,4,
};
/* code to quantized spectrum value */
static const float hcatbdecoder_read_val_table[128] = {
    +0.0f,+0.0f,+0.0f,+0.0f,+0.0f,+0.0f,+0.0f,+0.0f, +0.0f,+0.0f,+0.0f,+0.0f,+0.0f,+0.0f,+0.0f,+0.0f,
    +0.0f,+0.0f,+1.0f,-1.0f,+0.0f,+0.0f,+0.0f,+0.0f, +0.0f,+0.0f,+0.0f,+0.0f,+0.0f,+0.0f,+0.0f,+0.0f,
    +0.0f,+0.0f,+1.0f,+1.0f,-1.0f,-1.0f,+2.0f,-2.0f, +0.0f,+0.0f,+0.0f,+0.0f,+0.0f,+0.0f,+0.0f,+0.0f,
    +0.0f,+0.0f,+1.0f,-1.0f,+2.0f,-2.0f,+3.0f,-3.0f, +0.0f,+0.0f,+0.0f,+0.0f,+0.0f,+0.0f,+0.0f,+0.0f,
    +0.0f,+0.0f,+1.0f,+1.0f,-1.0f,-1.0f,+2.0f,+2.0f, -2.0f,-2.0f,+3.0f,+3.0f,-3.0f,-3.0f,+4.0f,-4.0f,
    +0.0f,+0.0f,+1.0f,+1.0f,-1.0f,-1.0f,+2.0f,+2.0f, -2.0f,-2.0f,+3.0f,-3.0f,+4.0f,-4.0f,+5.0f,-5.0f,
    +0.0f,+0.0f,+1.0f,+1.0f,-1.0f,-1.0f,+2.0f,-2.0f, +3.0f,-3.0f,+4.0f,-4.0f,+5.0f,-5.0f,+6.0f,-6.0f,
    +0.0f,+0.0f,+1.0f,-1.0f,+2.0f,-2.0f,+3.0f,-3.0f, +4.0f,-4.0f,+5.0f,-5.0f,+6.0f,-6.0f,+7.0f,-7.0f,
};

/* read spectral coefficients in the bitstream */
void dequantize_coefficients(stChannel* ch, clData* br, int subframe) {
    int i;
    unsigned int cc_count = ch->coded_count;

    for (i = 0; i < cc_count; i++) {
        float qc;
        unsigned char resolution = ch->resolution[i];
        unsigned char bits = hcatbdecoder_max_bit_table[resolution];
        unsigned int code = bitreader_read(br, bits);

        if (resolution > 7) {
            /* parse values in sign-magnitude form (lowest bit = sign) */
            int signed_code = (1 - ((code & 1) << 1)) * (code >> 1); /* move sign from low to up */
            if (signed_code == 0)
                bitreader_skip(br, -1); /* zero uses one less bit since it has no sign */
            qc = (float)signed_code;
        }
        else {
            /* use prefix codebooks for lower resolutions */
            int index = (resolution << 4) + code;
            int skip = hcatbdecoder_read_bit_table[index] - bits;
            bitreader_skip(br, skip);
            qc = hcatbdecoder_read_val_table[index];
        }

        /* dequantize coef with gain */
        ch->spectra[subframe][i] = ch->gain[i] * qc;
    }

    /* clean rest of spectra */
    memset(&ch->spectra[subframe][cc_count], 0, sizeof(ch->spectra[subframe][0]) * (HCA_SAMPLES_PER_SUBFRAME - cc_count));
}


//--------------------------------------------------
// Decode 3rd step
//--------------------------------------------------
/* in lib this table does start with a single 0.0 and adds + 63 below
 * (other decoders start with two 0.0 and add + 64 below, that should be equivalent) */
static const unsigned int hcadecoder_scale_conversion_table_hex[128] = {
    0x00000000,0x32A0B051,0x32D61B5E,0x330EA43A,0x333E0F68,0x337D3E0C,0x33A8B6D5,0x33E0CCDF,
    0x3415C3FF,0x34478D75,0x3484F1F6,0x34B123F6,0x34EC0719,0x351D3EDA,0x355184DF,0x358B95C2,
    0x35B9FCD2,0x35F7D0DF,0x36251958,0x365BFBB8,0x36928E72,0x36C346CD,0x370218AF,0x372D583F,
    0x3766F85B,0x3799E046,0x37CD078C,0x3808980F,0x38360094,0x38728177,0x38A18FAF,0x38D744FD,
    0x390F6A81,0x393F179A,0x397E9E11,0x39A9A15B,0x39E2055B,0x3A16942D,0x3A48A2D8,0x3A85AAC3,
    0x3AB21A32,0x3AED4F30,0x3B1E196E,0x3B52A81E,0x3B8C57CA,0x3BBAFF5B,0x3BF9295A,0x3C25FED7,
    0x3C5D2D82,0x3C935A2B,0x3CC4563F,0x3D02CD87,0x3D2E4934,0x3D68396A,0x3D9AB62B,0x3DCE248C,
    0x3E0955EE,0x3E36FD92,0x3E73D290,0x3EA27043,0x3ED87039,0x3F1031DC,0x3F40213B,0x3F800000,

    0x3FAA8D26,0x3FE33F89,0x4017657D,0x4049B9BE,0x40866491,0x40B311C4,0x40EE9910,0x411EF532,
    0x4153CCF1,0x418D1ADF,0x41BC034A,0x41FA83B3,0x4226E595,0x425E60F5,0x429426FF,0x42C5672A,
    0x43038359,0x432F3B79,0x43697C38,0x439B8D3A,0x43CF4319,0x440A14D5,0x4437FBF0,0x4475257D,
    0x44A3520F,0x44D99D16,0x4510FA4D,0x45412C4D,0x4580B1ED,0x45AB7A3A,0x45E47B6D,0x461837F0,
    0x464AD226,0x46871F62,0x46B40AAF,0x46EFE4BA,0x471FD228,0x4754F35B,0x478DDF04,0x47BD08A4,
    0x47FBDFED,0x4827CD94,0x485F9613,0x4894F4F0,0x48C67991,0x49043A29,0x49302F0E,0x496AC0C7,
    0x499C6573,0x49D06334,0x4A0AD4C6,0x4A38FBAF,0x4A767A41,0x4AA43516,0x4ADACB94,0x4B11C3D3,
    0x4B4238D2,0x4B8164D2,0x4BAC6897,0x4BE5B907,0x4C190B88,0x4C4BEC15,0x00000000,0x00000000,
};
static const float* hcadecoder_scale_conversion_table = (const float*)hcadecoder_scale_conversion_table_hex;

/* recreate resolution 0 coefs (not encoded) with pseudo-random noise based on
 * other coefs/scales (probably similar to AAC's perceptual noise substitution) */
void reconstruct_noise(stChannel* ch, unsigned int min_resolution, unsigned int ms_stereo, unsigned int* random_p, int subframe) {
    if (min_resolution > 0) /* added in v3.0 */
        return;
    if (ch->valid_count <= 0 || ch->noise_count <= 0)
        return;
    if (!(!ms_stereo || ch->type == STEREO_PRIMARY))
        return;

    {
        int i;
        int random_index, noise_index, valid_index, sf_noise, sf_valid, sc_index;
        unsigned int random = *random_p;

        for (i = 0; i < ch->noise_count; i++) {
            random = 0x343FD * random + 0x269EC3; /* typical rand() */

            random_index = HCA_SAMPLES_PER_SUBFRAME - ch->valid_count + (((random & 0x7FFF) * ch->valid_count) >> 15); /* can't go over 128 */

            /* points to next resolution 0 index and random non-resolution 0 index */
            noise_index = ch->noises[i];
            valid_index = ch->noises[random_index];

            /* get final scale index */
            sf_noise = ch->scalefactors[noise_index];
            sf_valid = ch->scalefactors[valid_index];
            sc_index = (sf_noise - sf_valid + 62) & ~((sf_noise - sf_valid + 62) >> 31);

            ch->spectra[subframe][noise_index] =
                hcadecoder_scale_conversion_table[sc_index] * ch->spectra[subframe][valid_index];
        }

        *random_p = random; /* lib saves this in the bitreader, maybe for simplified passing around */
    }
}

/* recreate missing coefs in high bands based on lower bands (probably similar to AAC's spectral band replication) */
void reconstruct_high_frequency(stChannel* ch, unsigned int hfr_group_count, unsigned int bands_per_hfr_group,
    unsigned int stereo_band_count, unsigned int base_band_count, unsigned int total_band_count, unsigned int version, int subframe) {
    if (bands_per_hfr_group == 0) /* added in v2.0, skipped in v2.0 files with 0 bands too */
        return;
    if (ch->type == STEREO_SECONDARY)
        return;

    {
        int i;
        int group, group_limit;
        int start_band = stereo_band_count + base_band_count;
        int highband = start_band;
        int lowband = start_band - 1;
        int sc_index;
        //unsigned char* hfr_scales = &ch->scalefactors[base_band_count + stereo_band_count]; /* v2.0 lib */
        unsigned char* hfr_scales = &ch->scalefactors[128 - hfr_group_count]; /* v3.0 lib */

        if (version <= HCA_VERSION_V200) {
            group_limit = hfr_group_count;
        }
        else {
            group_limit = (hfr_group_count >= 0) ? hfr_group_count : hfr_group_count + 1; /* ??? */
            group_limit = group_limit >> 1;
        }

        for (group = 0; group < hfr_group_count; group++) {
            int lowband_sub = (group < group_limit) ? 1 : 0; /* move lowband towards 0 until group reachs limit */

            for (i = 0; i < bands_per_hfr_group; i++) {
                if (highband >= total_band_count || lowband < 0)
                    break;

                sc_index = hfr_scales[group] - ch->scalefactors[lowband] + 63;
                sc_index = sc_index & ~(sc_index >> 31); /* clamped in v3.0 lib (in theory 6b sf are 0..128) */

                ch->spectra[subframe][highband] = hcadecoder_scale_conversion_table[sc_index] * ch->spectra[subframe][lowband];

                highband += 1;
                lowband -= lowband_sub;
            }
        }

        /* last spectrum coefficient is 0 (normally highband = 128, but perhaps could 'break' before) */
        ch->spectra[subframe][highband - 1] = 0.0f;
    }
}

//--------------------------------------------------
// Decode 4th step
//--------------------------------------------------
/* index to scale */
static const unsigned int hcadecoder_intensity_ratio_table_hex[16] = { /* max 4b */
    0x40000000,0x3FEDB6DB,0x3FDB6DB7,0x3FC92492,0x3FB6DB6E,0x3FA49249,0x3F924925,0x3F800000,
    0x3F5B6DB7,0x3F36DB6E,0x3F124925,0x3EDB6DB7,0x3E924925,0x3E124925,0x00000000,0x00000000,
};
static const float* hcadecoder_intensity_ratio_table = (const float*)hcadecoder_intensity_ratio_table_hex;

/* restore L/R bands based on channel coef + panning */
void apply_intensity_stereo(stChannel* ch_pair, int subframe, unsigned int base_band_count, unsigned int total_band_count) {
    if (ch_pair[0].type != STEREO_PRIMARY)
        return;

    {
        int band;
        float ratio_l = hcadecoder_intensity_ratio_table[ch_pair[1].intensity[subframe]];
        float ratio_r = 2.0f - ratio_l; /* correct, though other decoders substract 2.0 (it does use 'fsubr 2.0' and such) */
        float* sp_l = &ch_pair[0].spectra[subframe][0];
        float* sp_r = &ch_pair[1].spectra[subframe][0];

        for (band = base_band_count; band < total_band_count; band++) {
            float coef_l = sp_l[band] * ratio_l;
            float coef_r = sp_l[band] * ratio_r;
            sp_l[band] = coef_l;
            sp_r[band] = coef_r;
        }
    }
}

/* restore L/R bands based on mid channel + side differences */
void apply_ms_stereo(stChannel* ch_pair, unsigned int ms_stereo, unsigned int base_band_count, unsigned int total_band_count, int subframe) {
    if (!ms_stereo) /* added in v3.0 */
        return;
    if (ch_pair[0].type != STEREO_PRIMARY)
        return;

    {
        int band;
        const float ratio = 0.70710676908493; /* 0x3F3504F3 */
        float* sp_l = &ch_pair[0].spectra[subframe][0];
        float* sp_r = &ch_pair[1].spectra[subframe][0];

        for (band = base_band_count; band < total_band_count; band++) {
            float coef_l = (sp_l[band] + sp_r[band]) * ratio;
            float coef_r = (sp_l[band] - sp_r[band]) * ratio;
            sp_l[band] = coef_l;
            sp_r[band] = coef_r;
        }
    }
}

//--------------------------------------------------
// Decode 5th step
//--------------------------------------------------
static const unsigned int sin_tables_hex[7][64] = {
    {
        0x3DA73D75,0x3DA73D75,0x3DA73D75,0x3DA73D75,0x3DA73D75,0x3DA73D75,0x3DA73D75,0x3DA73D75,
        0x3DA73D75,0x3DA73D75,0x3DA73D75,0x3DA73D75,0x3DA73D75,0x3DA73D75,0x3DA73D75,0x3DA73D75,
        0x3DA73D75,0x3DA73D75,0x3DA73D75,0x3DA73D75,0x3DA73D75,0x3DA73D75,0x3DA73D75,0x3DA73D75,
        0x3DA73D75,0x3DA73D75,0x3DA73D75,0x3DA73D75,0x3DA73D75,0x3DA73D75,0x3DA73D75,0x3DA73D75,
        0x3DA73D75,0x3DA73D75,0x3DA73D75,0x3DA73D75,0x3DA73D75,0x3DA73D75,0x3DA73D75,0x3DA73D75,
        0x3DA73D75,0x3DA73D75,0x3DA73D75,0x3DA73D75,0x3DA73D75,0x3DA73D75,0x3DA73D75,0x3DA73D75,
        0x3DA73D75,0x3DA73D75,0x3DA73D75,0x3DA73D75,0x3DA73D75,0x3DA73D75,0x3DA73D75,0x3DA73D75,
        0x3DA73D75,0x3DA73D75,0x3DA73D75,0x3DA73D75,0x3DA73D75,0x3DA73D75,0x3DA73D75,0x3DA73D75,
    },{
        0x3F7B14BE,0x3F54DB31,0x3F7B14BE,0x3F54DB31,0x3F7B14BE,0x3F54DB31,0x3F7B14BE,0x3F54DB31,
        0x3F7B14BE,0x3F54DB31,0x3F7B14BE,0x3F54DB31,0x3F7B14BE,0x3F54DB31,0x3F7B14BE,0x3F54DB31,
        0x3F7B14BE,0x3F54DB31,0x3F7B14BE,0x3F54DB31,0x3F7B14BE,0x3F54DB31,0x3F7B14BE,0x3F54DB31,
        0x3F7B14BE,0x3F54DB31,0x3F7B14BE,0x3F54DB31,0x3F7B14BE,0x3F54DB31,0x3F7B14BE,0x3F54DB31,
        0x3F7B14BE,0x3F54DB31,0x3F7B14BE,0x3F54DB31,0x3F7B14BE,0x3F54DB31,0x3F7B14BE,0x3F54DB31,
        0x3F7B14BE,0x3F54DB31,0x3F7B14BE,0x3F54DB31,0x3F7B14BE,0x3F54DB31,0x3F7B14BE,0x3F54DB31,
        0x3F7B14BE,0x3F54DB31,0x3F7B14BE,0x3F54DB31,0x3F7B14BE,0x3F54DB31,0x3F7B14BE,0x3F54DB31,
        0x3F7B14BE,0x3F54DB31,0x3F7B14BE,0x3F54DB31,0x3F7B14BE,0x3F54DB31,0x3F7B14BE,0x3F54DB31,
    },{
        0x3F7EC46D,0x3F74FA0B,0x3F61C598,0x3F45E403,0x3F7EC46D,0x3F74FA0B,0x3F61C598,0x3F45E403,
        0x3F7EC46D,0x3F74FA0B,0x3F61C598,0x3F45E403,0x3F7EC46D,0x3F74FA0B,0x3F61C598,0x3F45E403,
        0x3F7EC46D,0x3F74FA0B,0x3F61C598,0x3F45E403,0x3F7EC46D,0x3F74FA0B,0x3F61C598,0x3F45E403,
        0x3F7EC46D,0x3F74FA0B,0x3F61C598,0x3F45E403,0x3F7EC46D,0x3F74FA0B,0x3F61C598,0x3F45E403,
        0x3F7EC46D,0x3F74FA0B,0x3F61C598,0x3F45E403,0x3F7EC46D,0x3F74FA0B,0x3F61C598,0x3F45E403,
        0x3F7EC46D,0x3F74FA0B,0x3F61C598,0x3F45E403,0x3F7EC46D,0x3F74FA0B,0x3F61C598,0x3F45E403,
        0x3F7EC46D,0x3F74FA0B,0x3F61C598,0x3F45E403,0x3F7EC46D,0x3F74FA0B,0x3F61C598,0x3F45E403,
        0x3F7EC46D,0x3F74FA0B,0x3F61C598,0x3F45E403,0x3F7EC46D,0x3F74FA0B,0x3F61C598,0x3F45E403,
    },{
        0x3F7FB10F,0x3F7D3AAC,0x3F7853F8,0x3F710908,0x3F676BD8,0x3F5B941A,0x3F4D9F02,0x3F3DAEF9,
        0x3F7FB10F,0x3F7D3AAC,0x3F7853F8,0x3F710908,0x3F676BD8,0x3F5B941A,0x3F4D9F02,0x3F3DAEF9,
        0x3F7FB10F,0x3F7D3AAC,0x3F7853F8,0x3F710908,0x3F676BD8,0x3F5B941A,0x3F4D9F02,0x3F3DAEF9,
        0x3F7FB10F,0x3F7D3AAC,0x3F7853F8,0x3F710908,0x3F676BD8,0x3F5B941A,0x3F4D9F02,0x3F3DAEF9,
        0x3F7FB10F,0x3F7D3AAC,0x3F7853F8,0x3F710908,0x3F676BD8,0x3F5B941A,0x3F4D9F02,0x3F3DAEF9,
        0x3F7FB10F,0x3F7D3AAC,0x3F7853F8,0x3F710908,0x3F676BD8,0x3F5B941A,0x3F4D9F02,0x3F3DAEF9,
        0x3F7FB10F,0x3F7D3AAC,0x3F7853F8,0x3F710908,0x3F676BD8,0x3F5B941A,0x3F4D9F02,0x3F3DAEF9,
        0x3F7FB10F,0x3F7D3AAC,0x3F7853F8,0x3F710908,0x3F676BD8,0x3F5B941A,0x3F4D9F02,0x3F3DAEF9,
    },{
        0x3F7FEC43,0x3F7F4E6D,0x3F7E1324,0x3F7C3B28,0x3F79C79D,0x3F76BA07,0x3F731447,0x3F6ED89E,
        0x3F6A09A7,0x3F64AA59,0x3F5EBE05,0x3F584853,0x3F514D3D,0x3F49D112,0x3F41D870,0x3F396842,
        0x3F7FEC43,0x3F7F4E6D,0x3F7E1324,0x3F7C3B28,0x3F79C79D,0x3F76BA07,0x3F731447,0x3F6ED89E,
        0x3F6A09A7,0x3F64AA59,0x3F5EBE05,0x3F584853,0x3F514D3D,0x3F49D112,0x3F41D870,0x3F396842,
        0x3F7FEC43,0x3F7F4E6D,0x3F7E1324,0x3F7C3B28,0x3F79C79D,0x3F76BA07,0x3F731447,0x3F6ED89E,
        0x3F6A09A7,0x3F64AA59,0x3F5EBE05,0x3F584853,0x3F514D3D,0x3F49D112,0x3F41D870,0x3F396842,
        0x3F7FEC43,0x3F7F4E6D,0x3F7E1324,0x3F7C3B28,0x3F79C79D,0x3F76BA07,0x3F731447,0x3F6ED89E,
        0x3F6A09A7,0x3F64AA59,0x3F5EBE05,0x3F584853,0x3F514D3D,0x3F49D112,0x3F41D870,0x3F396842,
    },{
        0x3F7FFB11,0x3F7FD397,0x3F7F84AB,0x3F7F0E58,0x3F7E70B0,0x3F7DABCC,0x3F7CBFC9,0x3F7BACCD,
        0x3F7A7302,0x3F791298,0x3F778BC5,0x3F75DEC6,0x3F740BDD,0x3F721352,0x3F6FF573,0x3F6DB293,
        0x3F6B4B0C,0x3F68BF3C,0x3F660F88,0x3F633C5A,0x3F604621,0x3F5D2D53,0x3F59F26A,0x3F5695E5,
        0x3F531849,0x3F4F7A1F,0x3F4BBBF8,0x3F47DE65,0x3F43E200,0x3F3FC767,0x3F3B8F3B,0x3F373A23,
        0x3F7FFB11,0x3F7FD397,0x3F7F84AB,0x3F7F0E58,0x3F7E70B0,0x3F7DABCC,0x3F7CBFC9,0x3F7BACCD,
        0x3F7A7302,0x3F791298,0x3F778BC5,0x3F75DEC6,0x3F740BDD,0x3F721352,0x3F6FF573,0x3F6DB293,
        0x3F6B4B0C,0x3F68BF3C,0x3F660F88,0x3F633C5A,0x3F604621,0x3F5D2D53,0x3F59F26A,0x3F5695E5,
        0x3F531849,0x3F4F7A1F,0x3F4BBBF8,0x3F47DE65,0x3F43E200,0x3F3FC767,0x3F3B8F3B,0x3F373A23,
    },{
        0x3F7FFEC4,0x3F7FF4E6,0x3F7FE129,0x3F7FC38F,0x3F7F9C18,0x3F7F6AC7,0x3F7F2F9D,0x3F7EEA9D,
        0x3F7E9BC9,0x3F7E4323,0x3F7DE0B1,0x3F7D7474,0x3F7CFE73,0x3F7C7EB0,0x3F7BF531,0x3F7B61FC,
        0x3F7AC516,0x3F7A1E84,0x3F796E4E,0x3F78B47B,0x3F77F110,0x3F772417,0x3F764D97,0x3F756D97,
        0x3F748422,0x3F73913F,0x3F7294F8,0x3F718F57,0x3F708066,0x3F6F6830,0x3F6E46BE,0x3F6D1C1D,
        0x3F6BE858,0x3F6AAB7B,0x3F696591,0x3F6816A8,0x3F66BECC,0x3F655E0B,0x3F63F473,0x3F628210,
        0x3F6106F2,0x3F5F8327,0x3F5DF6BE,0x3F5C61C7,0x3F5AC450,0x3F591E6A,0x3F577026,0x3F55B993,
        0x3F53FAC3,0x3F5233C6,0x3F5064AF,0x3F4E8D90,0x3F4CAE79,0x3F4AC77F,0x3F48D8B3,0x3F46E22A,
        0x3F44E3F5,0x3F42DE29,0x3F40D0DA,0x3F3EBC1B,0x3F3CA003,0x3F3A7CA4,0x3F385216,0x3F36206C,
    }
};
static const unsigned int cos_tables_hex[7][64] = {
    {
        0xBD0A8BD4,0x3D0A8BD4,0x3D0A8BD4,0xBD0A8BD4,0x3D0A8BD4,0xBD0A8BD4,0xBD0A8BD4,0x3D0A8BD4,
        0x3D0A8BD4,0xBD0A8BD4,0xBD0A8BD4,0x3D0A8BD4,0xBD0A8BD4,0x3D0A8BD4,0x3D0A8BD4,0xBD0A8BD4,
        0x3D0A8BD4,0xBD0A8BD4,0xBD0A8BD4,0x3D0A8BD4,0xBD0A8BD4,0x3D0A8BD4,0x3D0A8BD4,0xBD0A8BD4,
        0xBD0A8BD4,0x3D0A8BD4,0x3D0A8BD4,0xBD0A8BD4,0x3D0A8BD4,0xBD0A8BD4,0xBD0A8BD4,0x3D0A8BD4,
        0x3D0A8BD4,0xBD0A8BD4,0xBD0A8BD4,0x3D0A8BD4,0xBD0A8BD4,0x3D0A8BD4,0x3D0A8BD4,0xBD0A8BD4,
        0xBD0A8BD4,0x3D0A8BD4,0x3D0A8BD4,0xBD0A8BD4,0x3D0A8BD4,0xBD0A8BD4,0xBD0A8BD4,0x3D0A8BD4,
        0xBD0A8BD4,0x3D0A8BD4,0x3D0A8BD4,0xBD0A8BD4,0x3D0A8BD4,0xBD0A8BD4,0xBD0A8BD4,0x3D0A8BD4,
        0x3D0A8BD4,0xBD0A8BD4,0xBD0A8BD4,0x3D0A8BD4,0xBD0A8BD4,0x3D0A8BD4,0x3D0A8BD4,0xBD0A8BD4,
    },{
        0xBE47C5C2,0xBF0E39DA,0x3E47C5C2,0x3F0E39DA,0x3E47C5C2,0x3F0E39DA,0xBE47C5C2,0xBF0E39DA,
        0x3E47C5C2,0x3F0E39DA,0xBE47C5C2,0xBF0E39DA,0xBE47C5C2,0xBF0E39DA,0x3E47C5C2,0x3F0E39DA,
        0x3E47C5C2,0x3F0E39DA,0xBE47C5C2,0xBF0E39DA,0xBE47C5C2,0xBF0E39DA,0x3E47C5C2,0x3F0E39DA,
        0xBE47C5C2,0xBF0E39DA,0x3E47C5C2,0x3F0E39DA,0x3E47C5C2,0x3F0E39DA,0xBE47C5C2,0xBF0E39DA,
        0x3E47C5C2,0x3F0E39DA,0xBE47C5C2,0xBF0E39DA,0xBE47C5C2,0xBF0E39DA,0x3E47C5C2,0x3F0E39DA,
        0xBE47C5C2,0xBF0E39DA,0x3E47C5C2,0x3F0E39DA,0x3E47C5C2,0x3F0E39DA,0xBE47C5C2,0xBF0E39DA,
        0xBE47C5C2,0xBF0E39DA,0x3E47C5C2,0x3F0E39DA,0x3E47C5C2,0x3F0E39DA,0xBE47C5C2,0xBF0E39DA,
        0x3E47C5C2,0x3F0E39DA,0xBE47C5C2,0xBF0E39DA,0xBE47C5C2,0xBF0E39DA,0x3E47C5C2,0x3F0E39DA,
    },{
        0xBDC8BD36,0xBE94A031,0xBEF15AEA,0xBF226799,0x3DC8BD36,0x3E94A031,0x3EF15AEA,0x3F226799,
        0x3DC8BD36,0x3E94A031,0x3EF15AEA,0x3F226799,0xBDC8BD36,0xBE94A031,0xBEF15AEA,0xBF226799,
        0x3DC8BD36,0x3E94A031,0x3EF15AEA,0x3F226799,0xBDC8BD36,0xBE94A031,0xBEF15AEA,0xBF226799,
        0xBDC8BD36,0xBE94A031,0xBEF15AEA,0xBF226799,0x3DC8BD36,0x3E94A031,0x3EF15AEA,0x3F226799,
        0x3DC8BD36,0x3E94A031,0x3EF15AEA,0x3F226799,0xBDC8BD36,0xBE94A031,0xBEF15AEA,0xBF226799,
        0xBDC8BD36,0xBE94A031,0xBEF15AEA,0xBF226799,0x3DC8BD36,0x3E94A031,0x3EF15AEA,0x3F226799,
        0xBDC8BD36,0xBE94A031,0xBEF15AEA,0xBF226799,0x3DC8BD36,0x3E94A031,0x3EF15AEA,0x3F226799,
        0x3DC8BD36,0x3E94A031,0x3EF15AEA,0x3F226799,0xBDC8BD36,0xBE94A031,0xBEF15AEA,0xBF226799,
    },{
        0xBD48FB30,0xBE164083,0xBE78CFCC,0xBEAC7CD4,0xBEDAE880,0xBF039C3D,0xBF187FC0,0xBF2BEB4A,
        0x3D48FB30,0x3E164083,0x3E78CFCC,0x3EAC7CD4,0x3EDAE880,0x3F039C3D,0x3F187FC0,0x3F2BEB4A,
        0x3D48FB30,0x3E164083,0x3E78CFCC,0x3EAC7CD4,0x3EDAE880,0x3F039C3D,0x3F187FC0,0x3F2BEB4A,
        0xBD48FB30,0xBE164083,0xBE78CFCC,0xBEAC7CD4,0xBEDAE880,0xBF039C3D,0xBF187FC0,0xBF2BEB4A,
        0x3D48FB30,0x3E164083,0x3E78CFCC,0x3EAC7CD4,0x3EDAE880,0x3F039C3D,0x3F187FC0,0x3F2BEB4A,
        0xBD48FB30,0xBE164083,0xBE78CFCC,0xBEAC7CD4,0xBEDAE880,0xBF039C3D,0xBF187FC0,0xBF2BEB4A,
        0xBD48FB30,0xBE164083,0xBE78CFCC,0xBEAC7CD4,0xBEDAE880,0xBF039C3D,0xBF187FC0,0xBF2BEB4A,
        0x3D48FB30,0x3E164083,0x3E78CFCC,0x3EAC7CD4,0x3EDAE880,0x3F039C3D,0x3F187FC0,0x3F2BEB4A,
    },{
        0xBCC90AB0,0xBD96A905,0xBDFAB273,0xBE2F10A2,0xBE605C13,0xBE888E93,0xBEA09AE5,0xBEB8442A,
        0xBECF7BCA,0xBEE63375,0xBEFC5D27,0xBF08F59B,0xBF13682A,0xBF1D7FD1,0xBF273656,0xBF3085BB,
        0x3CC90AB0,0x3D96A905,0x3DFAB273,0x3E2F10A2,0x3E605C13,0x3E888E93,0x3EA09AE5,0x3EB8442A,
        0x3ECF7BCA,0x3EE63375,0x3EFC5D27,0x3F08F59B,0x3F13682A,0x3F1D7FD1,0x3F273656,0x3F3085BB,
        0x3CC90AB0,0x3D96A905,0x3DFAB273,0x3E2F10A2,0x3E605C13,0x3E888E93,0x3EA09AE5,0x3EB8442A,
        0x3ECF7BCA,0x3EE63375,0x3EFC5D27,0x3F08F59B,0x3F13682A,0x3F1D7FD1,0x3F273656,0x3F3085BB,
        0xBCC90AB0,0xBD96A905,0xBDFAB273,0xBE2F10A2,0xBE605C13,0xBE888E93,0xBEA09AE5,0xBEB8442A,
        0xBECF7BCA,0xBEE63375,0xBEFC5D27,0xBF08F59B,0xBF13682A,0xBF1D7FD1,0xBF273656,0xBF3085BB,
    },{
        0xBC490E90,0xBD16C32C,0xBD7B2B74,0xBDAFB680,0xBDE1BC2E,0xBE09CF86,0xBE22ABB6,0xBE3B6ECF,
        0xBE541501,0xBE6C9A7F,0xBE827DC0,0xBE8E9A22,0xBE9AA086,0xBEA68F12,0xBEB263EF,0xBEBE1D4A,
        0xBEC9B953,0xBED53641,0xBEE0924F,0xBEEBCBBB,0xBEF6E0CB,0xBF00E7E4,0xBF064B82,0xBF0B9A6B,
        0xBF10D3CD,0xBF15F6D9,0xBF1B02C6,0xBF1FF6CB,0xBF24D225,0xBF299415,0xBF2E3BDE,0xBF32C8C9,
        0x3C490E90,0x3D16C32C,0x3D7B2B74,0x3DAFB680,0x3DE1BC2E,0x3E09CF86,0x3E22ABB6,0x3E3B6ECF,
        0x3E541501,0x3E6C9A7F,0x3E827DC0,0x3E8E9A22,0x3E9AA086,0x3EA68F12,0x3EB263EF,0x3EBE1D4A,
        0x3EC9B953,0x3ED53641,0x3EE0924F,0x3EEBCBBB,0x3EF6E0CB,0x3F00E7E4,0x3F064B82,0x3F0B9A6B,
        0x3F10D3CD,0x3F15F6D9,0x3F1B02C6,0x3F1FF6CB,0x3F24D225,0x3F299415,0x3F2E3BDE,0x3F32C8C9,
    },{
        0xBBC90F88,0xBC96C9B6,0xBCFB49BA,0xBD2FE007,0xBD621469,0xBD8A200A,0xBDA3308C,0xBDBC3AC3,
        0xBDD53DB9,0xBDEE3876,0xBE039502,0xBE1008B7,0xBE1C76DE,0xBE28DEFC,0xBE354098,0xBE419B37,
        0xBE4DEE60,0xBE5A3997,0xBE667C66,0xBE72B651,0xBE7EE6E1,0xBE8586CE,0xBE8B9507,0xBE919DDD,
        0xBE97A117,0xBE9D9E78,0xBEA395C5,0xBEA986C4,0xBEAF713A,0xBEB554EC,0xBEBB31A0,0xBEC1071E,
        0xBEC6D529,0xBECC9B8B,0xBED25A09,0xBED8106B,0xBEDDBE79,0xBEE363FA,0xBEE900B7,0xBEEE9479,
        0xBEF41F07,0xBEF9A02D,0xBEFF17B2,0xBF0242B1,0xBF04F484,0xBF07A136,0xBF0A48AD,0xBF0CEAD0,
        0xBF0F8784,0xBF121EB0,0xBF14B039,0xBF173C07,0xBF19C200,0xBF1C420C,0xBF1EBC12,0xBF212FF9,
        0xBF239DA9,0xBF26050A,0xBF286605,0xBF2AC082,0xBF2D1469,0xBF2F61A5,0xBF31A81D,0xBF33E7BC,
    }
};

/* HCA window function, close to a KBD window with an alpha of around 3.82 (similar to AAC/Vorbis) */
static const unsigned int hcaimdct_window_float_hex[128] = {
    0x3A3504F0,0x3B0183B8,0x3B70C538,0x3BBB9268,0x3C04A809,0x3C308200,0x3C61284C,0x3C8B3F17,
    0x3CA83992,0x3CC77FBD,0x3CE91110,0x3D0677CD,0x3D198FC4,0x3D2DD35C,0x3D434643,0x3D59ECC1,
    0x3D71CBA8,0x3D85741E,0x3D92A413,0x3DA078B4,0x3DAEF522,0x3DBE1C9E,0x3DCDF27B,0x3DDE7A1D,
    0x3DEFB6ED,0x3E00D62B,0x3E0A2EDA,0x3E13E72A,0x3E1E00B1,0x3E287CF2,0x3E335D55,0x3E3EA321,
    0x3E4A4F75,0x3E56633F,0x3E62DF37,0x3E6FC3D1,0x3E7D1138,0x3E8563A2,0x3E8C72B7,0x3E93B561,
    0x3E9B2AEF,0x3EA2D26F,0x3EAAAAAB,0x3EB2B222,0x3EBAE706,0x3EC34737,0x3ECBD03D,0x3ED47F46,
    0x3EDD5128,0x3EE6425C,0x3EEF4EFF,0x3EF872D7,0x3F00D4A9,0x3F0576CA,0x3F0A1D3B,0x3F0EC548,
    0x3F136C25,0x3F180EF2,0x3F1CAAC2,0x3F213CA2,0x3F25C1A5,0x3F2A36E7,0x3F2E9998,0x3F32E705,

    0xBF371C9E,0xBF3B37FE,0xBF3F36F2,0xBF431780,0xBF46D7E6,0xBF4A76A4,0xBF4DF27C,0xBF514A6F,
    0xBF547DC5,0xBF578C03,0xBF5A74EE,0xBF5D3887,0xBF5FD707,0xBF6250DA,0xBF64A699,0xBF66D908,
    0xBF68E90E,0xBF6AD7B1,0xBF6CA611,0xBF6E5562,0xBF6FE6E7,0xBF715BEF,0xBF72B5D1,0xBF73F5E6,
    0xBF751D89,0xBF762E13,0xBF7728D7,0xBF780F20,0xBF78E234,0xBF79A34C,0xBF7A5397,0xBF7AF439,
    0xBF7B8648,0xBF7C0ACE,0xBF7C82C8,0xBF7CEF26,0xBF7D50CB,0xBF7DA88E,0xBF7DF737,0xBF7E3D86,
    0xBF7E7C2A,0xBF7EB3CC,0xBF7EE507,0xBF7F106C,0xBF7F3683,0xBF7F57CA,0xBF7F74B6,0xBF7F8DB6,
    0xBF7FA32E,0xBF7FB57B,0xBF7FC4F6,0xBF7FD1ED,0xBF7FDCAD,0xBF7FE579,0xBF7FEC90,0xBF7FF22E,
    0xBF7FF688,0xBF7FF9D0,0xBF7FFC32,0xBF7FFDDA,0xBF7FFEED,0xBF7FFF8F,0xBF7FFFDF,0xBF7FFFFC,
};
static const float* hcaimdct_window_float = (const float*)hcaimdct_window_float_hex;

/* apply DCT-IV to dequantized spectra to get final samples */
//HCAIMDCT_Transform
void imdct_transform(stChannel* ch, int subframe) {
    static const unsigned int size = HCA_SAMPLES_PER_SUBFRAME;
    static const unsigned int half = HCA_SAMPLES_PER_SUBFRAME / 2;
    static const unsigned int mdct_bits = HCA_MDCT_BITS;
    unsigned int i, j, k;

    /* This IMDCT (supposedly standard) is all too crafty for me to simplify, see VGAudio (Mdct.Dct4). */

    /* pre-pre-rotation(?) */
    {
        unsigned int count1 = 1;
        unsigned int count2 = half;
        float* temp1 = &ch->spectra[subframe][0];
        float* temp2 = &ch->temp[0];

        for (i = 0; i < mdct_bits; i++) {
            float* swap;
            float* d1 = &temp2[0];
            float* d2 = &temp2[count2];

            for (j = 0; j < count1; j++) {
                for (k = 0; k < count2; k++) {
                    float a = *(temp1++);
                    float b = *(temp1++);
                    *(d1++) = a + b;
                    *(d2++) = a - b;
                }
                d1 += count2;
                d2 += count2;
            }
            swap = temp1 - HCA_SAMPLES_PER_SUBFRAME; /* move spectra or temp to beginning */
            temp1 = temp2;
            temp2 = swap;

            count1 = count1 << 1;
            count2 = count2 >> 1;
        }
    }

    {
        unsigned int count1 = half;
        unsigned int count2 = 1;
        float* temp1 = &ch->temp[0];
        float* temp2 = &ch->spectra[subframe][0];

        for (i = 0; i < mdct_bits; i++) {
            const float* sin_table = (const float*)sin_tables_hex[i];//todo cleanup
            const float* cos_table = (const float*)cos_tables_hex[i];
            float* swap;
            float* d1 = &temp2[0];
            float* d2 = &temp2[count2 * 2 - 1];
            const float* s1 = &temp1[0];
            const float* s2 = &temp1[count2];

            for (j = 0; j < count1; j++) {
                for (k = 0; k < count2; k++) {
                    float a = *(s1++);
                    float b = *(s2++);
                    float sin = *(sin_table++);
                    float cos = *(cos_table++);
                    *(d1++) = a * sin - b * cos;
                    *(d2--) = a * cos + b * sin;
                }
                s1 += count2;
                s2 += count2;
                d1 += count2;
                d2 += count2 * 3;
            }
            swap = temp1;
            temp1 = temp2;
            temp2 = swap;

            count1 = count1 >> 1;
            count2 = count2 << 1;
        }
#if 0
        /* copy dct */
        /* (with the above optimization spectra is already modified, so this is redundant) */
        for (i = 0; i < size; i++) {
            ch->dct[i] = ch->spectra[subframe][i];
        }
#endif
    }

    /* update output/imdct with overlapped window (lib fuses this with the above) */
    {
        const float* dct = &ch->spectra[subframe][0]; //ch->dct;
        const float* prev = &ch->imdct_previous[0];

        for (i = 0; i < half; i++) {
            ch->wave[subframe][i] = hcaimdct_window_float[i] * dct[i + half] + prev[i];
            ch->wave[subframe][i + half] = hcaimdct_window_float[i + half] * dct[size - 1 - i] - prev[i + half];
            ch->imdct_previous[i] = hcaimdct_window_float[size - 1 - i] * dct[half - i - 1];
            ch->imdct_previous[i + half] = hcaimdct_window_float[half - i - 1] * dct[i];
        }
#if 0
        /* over-optimized IMDCT window (for reference), barely noticeable even when decoding hundred of files */
        const float* imdct_window = hcaimdct_window_float;
        const float* dct;
        float* imdct_previous;
        float* wave = ch->wave[subframe];

        dct = &ch->dct[half];
        imdct_previous = ch->imdct_previous;
        for (i = 0; i < half; i++) {
            *(wave++) = *(dct++) * *(imdct_window++) + *(imdct_previous++);
        }
        for (i = 0; i < half; i++) {
            *(wave++) = *(imdct_window++) * *(--dct) - *(imdct_previous++);
        }
        /* implicit: imdct_window pointer is now at end */
        dct = &ch->dct[half - 1];
        imdct_previous = ch->imdct_previous;
        for (i = 0; i < half; i++) {
            *(imdct_previous++) = *(--imdct_window) * *(dct--);
        }
        for (i = 0; i < half; i++) {
            *(imdct_previous++) = *(--imdct_window) * *(++dct);
        }
#endif
    }
}
