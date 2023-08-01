#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define HCA_SUBFRAMES  8
#define HCA_SAMPLES_PER_SUBFRAME  128   /* also spectrum points/etc */

    typedef enum { DISCRETE = 0, STEREO_PRIMARY = 1, STEREO_SECONDARY = 2 } channel_type_t;
    typedef struct clData {
        const unsigned char* data;
        int size;
        int bit;
    } clData;
    typedef struct stChannel {
        /* HCA channel config */
        channel_type_t type;
        unsigned int coded_count;                               /* encoded scales/resolutions/coefs */

        /* subframe state */
        unsigned char intensity[HCA_SUBFRAMES];                 /* intensity indexes for joins stereo (value max: 15 / 4b) */
        unsigned char scalefactors[HCA_SAMPLES_PER_SUBFRAME];   /* scale indexes (value max: 64 / 6b)*/
        unsigned char resolution[HCA_SAMPLES_PER_SUBFRAME];     /* resolution indexes (value max: 15 / 4b) */
        unsigned char noises[HCA_SAMPLES_PER_SUBFRAME];         /* indexes to coefs that need noise fill + coefs that don't (value max: 128 / 8b) */
        unsigned int noise_count;                               /* resolutions with noise values saved in 'noises' */
        unsigned int valid_count;                               /* resolutions with valid values saved in 'noises' */

        float gain[HCA_SAMPLES_PER_SUBFRAME];                   /* gain to apply to quantized spectral data */
        float spectra[HCA_SUBFRAMES][HCA_SAMPLES_PER_SUBFRAME]; /* resulting dequantized data */

        float temp[HCA_SAMPLES_PER_SUBFRAME];                   /* temp for DCT-IV */
        float dct[HCA_SAMPLES_PER_SUBFRAME];                    /* result of DCT-IV */
        float imdct_previous[HCA_SAMPLES_PER_SUBFRAME];         /* IMDCT */

        /* frame state */
        float wave[HCA_SUBFRAMES][HCA_SAMPLES_PER_SUBFRAME];  /* resulting samples */
    } stChannel;


    void bitreader_init(clData* br, const void* data, int size);
    unsigned int bitreader_read(clData* br, int bitsize);

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

#ifdef __cplusplus
}
#endif