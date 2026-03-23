#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "aff3ct_jl.h"

/* Simple PRNG (xorshift32) for reproducibility */
static unsigned int xor_state = 12345;
static unsigned int xorshift32(void) {
    xor_state ^= xor_state << 13;
    xor_state ^= xor_state >> 17;
    xor_state ^= xor_state << 5;
    return xor_state;
}
static float rand_uniform(void) {
    return (float)(xorshift32() & 0x7FFFFFFF) / (float)0x7FFFFFFF;
}
static float rand_gaussian(void) {
    float u1 = rand_uniform();
    float u2 = rand_uniform();
    if (u1 < 1e-10f) u1 = 1e-10f;
    return sqrtf(-2.0f * logf(u1)) * cosf(6.2831853f * u2);
}

int main(void) {
    int K = 128, N = 256;
    float design_snr = 2.0f;

    printf("=== libaff3ct_jl Polar code test ===\n");
    printf("Version: %s\n", aff3ct_jl_version());

    /* ── Generate frozen bits ──────────────────────────── */
    aff3ct_frozenbits_gen_t fbgen = aff3ct_frozenbits_gen_ga_create(K, N, design_snr);
    if (!fbgen) {
        fprintf(stderr, "ERROR: %s\n", aff3ct_jl_last_error());
        return 1;
    }

    int* frozen_bits = (int*)calloc(N, sizeof(int));
    if (aff3ct_frozenbits_gen_generate(fbgen, frozen_bits) != 0) {
        fprintf(stderr, "ERROR: %s\n", aff3ct_jl_last_error());
        return 1;
    }
    aff3ct_frozenbits_gen_destroy(fbgen);

    int n_frozen = 0;
    for (int i = 0; i < N; i++) n_frozen += frozen_bits[i];
    printf("Frozen bits: %d / %d  (K = %d)\n", n_frozen, N, K);

    /* ── Create encoder & SCL decoder ──────────────────── */
    aff3ct_encoder_t enc = aff3ct_polar_encoder_create(K, N, frozen_bits);
    if (!enc) { fprintf(stderr, "ERROR: %s\n", aff3ct_jl_last_error()); return 1; }

    aff3ct_decoder_t dec = aff3ct_polar_scl_decoder_create(K, N, 8, frozen_bits);
    if (!dec) { fprintf(stderr, "ERROR: %s\n", aff3ct_jl_last_error()); return 1; }

    /* ── Monte Carlo: multiple frames ──────────────────── */
    int total_errors = 0, total_bits = 0;
    int n_frames = 100;
    float sigma = 0.5f; /* Eb/N0 ≈ 3 dB for R=0.5 */

    int* U_K = (int*)calloc(K, sizeof(int));
    int* X_N = (int*)calloc(N, sizeof(int));
    float* Y_N = (float*)calloc(N, sizeof(float));
    int* V_K = (int*)calloc(K, sizeof(int));

    for (int f = 0; f < n_frames; f++) {
        for (int i = 0; i < K; i++)
            U_K[i] = (xorshift32() & 1);

        aff3ct_polar_encode(enc, U_K, X_N);

        for (int i = 0; i < N; i++) {
            float bpsk = 1.0f - 2.0f * (float)X_N[i];
            float received = bpsk + sigma * rand_gaussian();
            Y_N[i] = 2.0f * received / (sigma * sigma);
        }

        aff3ct_polar_scl_decode(dec, Y_N, V_K);

        for (int i = 0; i < K; i++)
            if (U_K[i] != V_K[i]) total_errors++;
        total_bits += K;
    }

    float ber = (float)total_errors / (float)total_bits;
    printf("Frames: %d, Total bit errors: %d / %d, BER: %.2e\n",
           n_frames, total_errors, total_bits, ber);

    if (ber < 1e-3f) {
        printf("PASS: BER below threshold\n");
    } else {
        printf("FAIL: BER too high (expected < 1e-3 at Eb/N0 ~ 3 dB with SCL-8)\n");
    }

    /* ── Cleanup ───────────────────────────────────────── */
    aff3ct_polar_encoder_destroy(enc);
    aff3ct_polar_scl_decoder_destroy(dec);
    free(frozen_bits); free(U_K); free(X_N); free(Y_N); free(V_K);

    return (ber < 1e-3f) ? 0 : 1;
}
