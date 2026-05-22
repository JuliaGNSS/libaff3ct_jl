#ifndef AFF3CT_JL_H
#define AFF3CT_JL_H

#ifdef __cplusplus
extern "C" {
#endif

/* ── Version / error ─────────────────────────────────────────────── */

const char* aff3ct_jl_version(void);

/* Returns last error message (per-thread), or NULL if no error. */
const char* aff3ct_jl_last_error(void);

/* ── Opaque handle types ─────────────────────────────────────────── */

typedef void* aff3ct_frozenbits_gen_t;
typedef void* aff3ct_encoder_t;
typedef void* aff3ct_decoder_t;
typedef void* aff3ct_sparse_matrix_t;

/* ── Polar: frozen bits generators ───────────────────────────────── */

/* Gaussian Approximation frozen bits generator.
 * K      – number of information bits
 * N      – codeword length (must be power of 2)
 * design_snr – design Eb/N0 in dB (used to set sigma) */
aff3ct_frozenbits_gen_t aff3ct_frozenbits_gen_ga_create(int K, int N, float design_snr);

/* 5G-NR frozen bits (N ≤ 1024). */
aff3ct_frozenbits_gen_t aff3ct_frozenbits_gen_5g_create(int K, int N);

/* Write frozen bits into caller-allocated array of length N (0=info, 1=frozen). */
int aff3ct_frozenbits_gen_generate(aff3ct_frozenbits_gen_t gen, int* frozen_bits);

void aff3ct_frozenbits_gen_destroy(aff3ct_frozenbits_gen_t gen);

/* ── Polar: encoder ──────────────────────────────────────────────── */

/* frozen_bits: int array of length N (0=info, 1=frozen). */
aff3ct_encoder_t aff3ct_polar_encoder_create(int K, int N, const int* frozen_bits);
int  aff3ct_polar_encode(aff3ct_encoder_t enc, const int* U_K, int* X_N);
void aff3ct_polar_encoder_destroy(aff3ct_encoder_t enc);

/* ── Polar: SC decoder ───────────────────────────────────────────── */

aff3ct_decoder_t aff3ct_polar_sc_decoder_create(int K, int N, const int* frozen_bits);
int  aff3ct_polar_sc_decode(aff3ct_decoder_t dec, const float* Y_N, int* V_K);
void aff3ct_polar_sc_decoder_destroy(aff3ct_decoder_t dec);

/* ── Polar: SCL decoder ──────────────────────────────────────────── */

/* L – list size. */
aff3ct_decoder_t aff3ct_polar_scl_decoder_create(int K, int N, int L, const int* frozen_bits);
int  aff3ct_polar_scl_decode(aff3ct_decoder_t dec, const float* Y_N, int* V_K);
void aff3ct_polar_scl_decoder_destroy(aff3ct_decoder_t dec);

/* ── LDPC: sparse matrix I/O ─────────────────────────────────────── */

/* Load parity-check matrix H from alist or QC file.
 * Also extracts info_bits_pos (length K) into caller-allocated array if non-NULL.
 * Returns the matrix handle, or NULL on error. */
aff3ct_sparse_matrix_t aff3ct_sparse_matrix_load(const char* filepath,
                                                  int* out_M, int* out_N,
                                                  unsigned int* info_bits_pos,
                                                  int info_bits_pos_len);

int  aff3ct_sparse_matrix_info_bits_count(aff3ct_sparse_matrix_t mat);
int  aff3ct_sparse_matrix_nrows(aff3ct_sparse_matrix_t mat);
int  aff3ct_sparse_matrix_ncols(aff3ct_sparse_matrix_t mat);
void aff3ct_sparse_matrix_destroy(aff3ct_sparse_matrix_t mat);

/* ── LDPC: encoder ───────────────────────────────────────────────── */

/* Create encoder from generator matrix G (sparse). */
aff3ct_encoder_t aff3ct_ldpc_encoder_create(int K, int N, aff3ct_sparse_matrix_t H);
int  aff3ct_ldpc_encode(aff3ct_encoder_t enc, const int* U_K, int* X_N);
void aff3ct_ldpc_encoder_destroy(aff3ct_encoder_t enc);

/* ── LDPC: BP flooding SPA decoder ───────────────────────────────── */

/* info_bits_pos: array of length K with positions of info bits in the codeword. */
aff3ct_decoder_t aff3ct_ldpc_bp_decoder_create(int K, int N, int n_ite,
                                                aff3ct_sparse_matrix_t H,
                                                const unsigned int* info_bits_pos);
int  aff3ct_ldpc_bp_decode(aff3ct_decoder_t dec, const float* Y_N, int* V_K);
void aff3ct_ldpc_bp_decoder_destroy(aff3ct_decoder_t dec);

/* ── RSC: encoder ───────────────────────────────────────────────── */

/* Stand-alone RSC (Recursive Systematic Convolutional) encoder.
 * Output is interleaved: [s0,p0,s1,p1,...,tail_s0,tail_p0,...].
 * Trellis is always terminated (tail bits appended).
 * poly: generator polynomial pair in octal, e.g. {05, 07}. Pass NULL for default {05,07}.
 * poly_len: length of poly array (must be 2 if non-NULL). */
aff3ct_encoder_t aff3ct_rsc_encoder_create(int K, int N,
                                            const int* poly, int poly_len);
int  aff3ct_rsc_encode(aff3ct_encoder_t enc, const int* U_K, int* X_N);
void aff3ct_rsc_encoder_destroy(aff3ct_encoder_t enc);

/* ── Viterbi: decoder ───────────────────────────────────────────── */

/* Viterbi (SIHO) decoder for RSC codes.
 * Expects interleaved LLRs matching RSC encoder output format.
 * Uses same polynomial as the RSC encoder. */
aff3ct_decoder_t aff3ct_viterbi_decoder_create(int K, int N,
                                                const int* poly, int poly_len);
int  aff3ct_viterbi_decode(aff3ct_decoder_t dec, const float* Y_N, int* V_K);
void aff3ct_viterbi_decoder_destroy(aff3ct_decoder_t dec);

/* ── Feedforward convolutional: encoder ─────────────────────────── */

/* Stand-alone feedforward convolutional encoder (non-systematic),
 * e.g. Galileo E1B with poly = {0171, 0133}. Trellis is always
 * terminated (tail bits appended).
 *
 * Constraint: N must equal n_poly * (K + n_ff), where
 *   n_poly = poly_len, n_ff = floor(log2(max(poly))).
 *
 * poly: generator polynomials in octal (rate = 1/poly_len), required.
 * poly_len: number of polynomials (>= 2). */
aff3ct_encoder_t aff3ct_conv_encoder_create(int K, int N,
                                             const int* poly, int poly_len);
int  aff3ct_conv_encode(aff3ct_encoder_t enc, const int* U_K, int* X_N);
void aff3ct_conv_encoder_destroy(aff3ct_encoder_t enc);

/* ── Viterbi: decoder for feedforward convolutional codes ───────── */

/* Distinct from aff3ct_viterbi_decoder_create because the trellis
 * differs between RSC and feedforward codes. */
aff3ct_decoder_t aff3ct_conv_viterbi_decoder_create(int K, int N,
                                                     const int* poly, int poly_len);
int  aff3ct_conv_viterbi_decode(aff3ct_decoder_t dec, const float* Y_N, int* V_K);
void aff3ct_conv_viterbi_decoder_destroy(aff3ct_decoder_t dec);

/* ── Turbo: encoder ──────────────────────────────────────────────── */

/* interleaver_type: "LTE", "RANDOM", or "NO" (identity).
 * poly: generator polynomial pair, e.g. {013, 015} in octal. Pass NULL for default {013,015}.
 * poly_len: length of poly array (must be 2 if non-NULL). */
aff3ct_encoder_t aff3ct_turbo_encoder_create(int K, int N,
                                              const char* interleaver_type,
                                              const int* poly, int poly_len);
int  aff3ct_turbo_encode(aff3ct_encoder_t enc, const int* U_K, int* X_N);
void aff3ct_turbo_encoder_destroy(aff3ct_encoder_t enc);

/* ── Turbo: decoder ──────────────────────────────────────────────── */

aff3ct_decoder_t aff3ct_turbo_decoder_create(int K, int N, int n_ite,
                                              const char* interleaver_type,
                                              const int* poly, int poly_len,
                                              int buffered_encoding);
int  aff3ct_turbo_decode(aff3ct_decoder_t dec, const float* Y_N, int* V_K);
void aff3ct_turbo_decoder_destroy(aff3ct_decoder_t dec);

#ifdef __cplusplus
}
#endif

#endif /* AFF3CT_JL_H */
