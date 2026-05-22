#include "aff3ct_jl.h"
#include "common.h"

#include <vector>
#include <memory>
#include <stdexcept>

// aff3ct headers
#include <Module/Encoder/RSC/Encoder_RSC_generic_sys.hpp>
#include <Module/Decoder/Conv/Viterbi/Decoder_Viterbi_SIHO.hpp>

using namespace aff3ct::module;

// ── helpers ──────────────────────────────────────────────────────────

static std::vector<int> get_rsc_poly(const int* poly, int poly_len) {
    if (poly && poly_len == 2)
        return {poly[0], poly[1]};
    // Default: constraint length 3, octal {05, 07}
    return {05, 07};
}

// ── Wrapper structs ─────────────────────────────────────────────────

struct RSCEncoderWrapper {
    std::unique_ptr<Encoder_RSC_generic_sys<int>> enc;
};

struct ViterbiDecoderWrapper {
    std::unique_ptr<Decoder_Viterbi_SIHO<int, float>> dec;
};

extern "C" {

// ── RSC encoder ─────────────────────────────────────────────────────
// Always uses non-buffered (interleaved) output: [s0,p0,s1,p1,...,ts0,tp0,...]
// This is the format expected by the Viterbi decoder.

aff3ct_encoder_t aff3ct_rsc_encoder_create(int K, int N,
                                            const int* poly, int poly_len) {
    aff3ct_jl_clear_error();
    try {
        auto w = new RSCEncoderWrapper();
        auto p = get_rsc_poly(poly, poly_len);
        // buffered=false → interleaved output matching Viterbi decoder input format
        w->enc = std::make_unique<Encoder_RSC_generic_sys<int>>(K, N, false, p);
        return static_cast<aff3ct_encoder_t>(w);
    } catch (const std::exception& e) {
        aff3ct_jl_set_error(e.what());
        return nullptr;
    }
}

int aff3ct_rsc_encode(aff3ct_encoder_t handle, const int* U_K, int* X_N) {
    aff3ct_jl_clear_error();
    try {
        auto w = static_cast<RSCEncoderWrapper*>(handle);
        w->enc->encode(U_K, X_N);
        return 0;
    } catch (const std::exception& e) {
        aff3ct_jl_set_error(e.what());
        return -1;
    }
}

void aff3ct_rsc_encoder_destroy(aff3ct_encoder_t handle) {
    if (!handle) return;
    auto w = static_cast<RSCEncoderWrapper*>(handle);
    delete w;
}

// ── Viterbi decoder ─────────────────────────────────────────────────
// Always uses is_closed=true (trellis terminated), matching the RSC encoder
// which always generates tail bits.

aff3ct_decoder_t aff3ct_viterbi_decoder_create(int K, int N,
                                                const int* poly, int poly_len) {
    aff3ct_jl_clear_error();
    try {
        auto w = new ViterbiDecoderWrapper();
        auto p = get_rsc_poly(poly, poly_len);

        // Create temporary RSC encoder to extract trellis (same pattern as turbo)
        Encoder_RSC_generic_sys<int> tmp_enc(K, N, false, p);
        auto trellis = tmp_enc.get_trellis();

        // is_closed=true → trellis terminated (tail bits present)
        w->dec = std::make_unique<Decoder_Viterbi_SIHO<int, float>>(K, trellis, true);
        return static_cast<aff3ct_decoder_t>(w);
    } catch (const std::exception& e) {
        aff3ct_jl_set_error(e.what());
        return nullptr;
    }
}

int aff3ct_viterbi_decode(aff3ct_decoder_t handle, const float* Y_N, int* V_K) {
    aff3ct_jl_clear_error();
    try {
        auto w = static_cast<ViterbiDecoderWrapper*>(handle);
        return w->dec->decode_siho(Y_N, V_K);
    } catch (const std::exception& e) {
        aff3ct_jl_set_error(e.what());
        return -1;
    }
}

void aff3ct_viterbi_decoder_destroy(aff3ct_decoder_t handle) {
    if (!handle) return;
    auto w = static_cast<ViterbiDecoderWrapper*>(handle);
    delete w;
}

} // extern "C"
