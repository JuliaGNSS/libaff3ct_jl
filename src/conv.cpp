#include "aff3ct_jl.h"
#include "common.h"

#include <vector>
#include <memory>
#include <stdexcept>

// aff3ct headers
#include <Module/Encoder/Conv/Encoder_conv.hpp>
#include <Module/Decoder/Conv/Viterbi/Decoder_Viterbi_SIHO.hpp>

using namespace aff3ct::module;

// ── Wrapper structs ─────────────────────────────────────────────────

struct ConvEncoderWrapper {
    std::unique_ptr<Encoder_conv<int>> enc;
};

struct ConvViterbiDecoderWrapper {
    std::unique_ptr<Decoder_Viterbi_SIHO<int, float>> dec;
};

extern "C" {

// ── Feedforward convolutional encoder ───────────────────────────────
// N must equal n_poly * (K + n_ff), where n_ff = floor(log2(max(poly))).
// Example: Galileo E1B uses poly = {0171, 0133} (constraint length 7 → n_ff = 6),
// rate 1/2 → for K = 64 → N = 2 * (64 + 6) = 140.

aff3ct_encoder_t aff3ct_conv_encoder_create(int K, int N,
                                             const int* poly, int poly_len) {
    aff3ct_jl_clear_error();
    try {
        if (!poly || poly_len < 2) {
            throw std::invalid_argument(
                "aff3ct_conv_encoder_create: poly must be non-null with poly_len >= 2");
        }
        auto w = new ConvEncoderWrapper();
        std::vector<int> p(poly, poly + poly_len);
        w->enc = std::make_unique<Encoder_conv<int>>(K, N, p);
        return static_cast<aff3ct_encoder_t>(w);
    } catch (const std::exception& e) {
        aff3ct_jl_set_error(e.what());
        return nullptr;
    }
}

int aff3ct_conv_encode(aff3ct_encoder_t handle, const int* U_K, int* X_N) {
    aff3ct_jl_clear_error();
    try {
        auto w = static_cast<ConvEncoderWrapper*>(handle);
        w->enc->encode(U_K, X_N);
        return 0;
    } catch (const std::exception& e) {
        aff3ct_jl_set_error(e.what());
        return -1;
    }
}

void aff3ct_conv_encoder_destroy(aff3ct_encoder_t handle) {
    if (!handle) return;
    auto w = static_cast<ConvEncoderWrapper*>(handle);
    delete w;
}

// ── Viterbi decoder for feedforward convolutional codes ─────────────
// The trellis differs from the RSC one (feedforward codes are non-systematic),
// so we instantiate Encoder_conv to extract get_trellis().

aff3ct_decoder_t aff3ct_conv_viterbi_decoder_create(int K, int N,
                                                     const int* poly, int poly_len) {
    aff3ct_jl_clear_error();
    try {
        if (!poly || poly_len < 2) {
            throw std::invalid_argument(
                "aff3ct_conv_viterbi_decoder_create: poly must be non-null with poly_len >= 2");
        }
        auto w = new ConvViterbiDecoderWrapper();
        std::vector<int> p(poly, poly + poly_len);

        Encoder_conv<int> tmp_enc(K, N, p);
        auto trellis = tmp_enc.get_trellis();

        // is_closed=true → trellis terminated (Encoder_conv always appends tail bits)
        w->dec = std::make_unique<Decoder_Viterbi_SIHO<int, float>>(K, trellis, true);
        return static_cast<aff3ct_decoder_t>(w);
    } catch (const std::exception& e) {
        aff3ct_jl_set_error(e.what());
        return nullptr;
    }
}

int aff3ct_conv_viterbi_decode(aff3ct_decoder_t handle, const float* Y_N, int* V_K) {
    aff3ct_jl_clear_error();
    try {
        auto w = static_cast<ConvViterbiDecoderWrapper*>(handle);
        return w->dec->decode_siho(Y_N, V_K);
    } catch (const std::exception& e) {
        aff3ct_jl_set_error(e.what());
        return -1;
    }
}

void aff3ct_conv_viterbi_decoder_destroy(aff3ct_decoder_t handle) {
    if (!handle) return;
    auto w = static_cast<ConvViterbiDecoderWrapper*>(handle);
    delete w;
}

} // extern "C"
