#include "aff3ct_jl.h"
#include "common.h"

#include <vector>
#include <string>
#include <memory>
#include <stdexcept>
#include <cstring>

// aff3ct headers
#include <Module/Encoder/Turbo/Encoder_turbo.hpp>
#include <Module/Encoder/RSC/Encoder_RSC_generic_sys.hpp>
#include <Module/Decoder/Turbo/Decoder_turbo_std.hpp>
#include <Module/Decoder/RSC/BCJR/Seq/Decoder_RSC_BCJR_seq_std.hpp>
#include <Module/Interleaver/Interleaver.hpp>
#include <Tools/Interleaver/LTE/Interleaver_core_LTE.hpp>
#include <Tools/Interleaver/Random/Interleaver_core_random.hpp>
#include <Tools/Interleaver/NO/Interleaver_core_NO.hpp>
#include <Tools/Code/Turbo/Post_processing_SISO/Scaling_factor/Scaling_factor_vec.hpp>

using namespace aff3ct::module;
using namespace aff3ct::tools;

// ── helpers ──────────────────────────────────────────────────────────

static std::unique_ptr<Interleaver_core<uint32_t>>
make_interleaver_core(const char* type, int K) {
    if (!type || strcmp(type, "LTE") == 0)
        return std::make_unique<Interleaver_core_LTE<uint32_t>>(K);
    if (strcmp(type, "RANDOM") == 0)
        return std::make_unique<Interleaver_core_random<uint32_t>>(K);
    if (strcmp(type, "NO") == 0)
        return std::make_unique<Interleaver_core_NO<uint32_t>>(K);
    throw std::runtime_error(std::string("Unknown interleaver type: ") + type);
}

static std::vector<int> get_poly(const int* poly, int poly_len) {
    if (poly && poly_len == 2)
        return {poly[0], poly[1]};
    // Default LTE polynomial (octal 013, 015)
    return {013, 015};
}

// ── Wrapper structs ─────────────────────────────────────────────────

// Turbo encoder needs to own the component objects
struct TurboEncoderWrapper {
    std::unique_ptr<Interleaver_core<uint32_t>> itl_core;
    std::unique_ptr<Interleaver<int>>           itl;
    std::unique_ptr<Encoder_RSC_generic_sys<int>> enc_n;
    std::unique_ptr<Encoder_RSC_generic_sys<int>> enc_i;
    Encoder_turbo<int>*                         enc;
};

struct TurboDecoderWrapper {
    std::unique_ptr<Interleaver_core<uint32_t>>   itl_core;
    std::unique_ptr<Interleaver<float>>           itl;  // Interleaver<R> for decoder
    std::unique_ptr<Decoder_RSC_BCJR_seq_std<int, float>> siso_n;
    std::unique_ptr<Decoder_RSC_BCJR_seq_std<int, float>> siso_i;
    Decoder_turbo_std<int, float>*                dec;
    std::unique_ptr<Scaling_factor_vec<int, float>> sf;
};

extern "C" {

// ── Turbo encoder ───────────────────────────────────────────────────

aff3ct_encoder_t aff3ct_turbo_encoder_create(int K, int N,
                                              const char* interleaver_type,
                                              const int* poly, int poly_len) {
    aff3ct_jl_clear_error();
    try {
        auto w = new TurboEncoderWrapper();

        auto p = get_poly(poly, poly_len);

        // RSC sub-encoder output length: K + tail_bits, with tail = 2 * n_ff per encoder
        // For poly {013,015}: n_ff = 3, tail per encoder = 2*3 = 6
        // N_rsc = 2*(K + n_ff) for each sub-encoder in buffered mode
        int n_ff = 0;
        int v = p[0] > p[1] ? p[0] : p[1];
        while (v > 1) { n_ff++; v >>= 1; }
        int N_rsc = 2 * (K + n_ff);

        w->enc_n = std::make_unique<Encoder_RSC_generic_sys<int>>(K, N_rsc, true, p);
        w->enc_i = std::make_unique<Encoder_RSC_generic_sys<int>>(K, N_rsc, true, p);

        w->itl_core = make_interleaver_core(interleaver_type, K);
        w->itl_core->refresh();
        w->itl = std::make_unique<Interleaver<int>>(*w->itl_core);

        w->enc = new Encoder_turbo<int>(K, N, *w->enc_n, *w->enc_i, *w->itl);
        return static_cast<aff3ct_encoder_t>(w);
    } catch (const std::exception& e) {
        aff3ct_jl_set_error(e.what());
        return nullptr;
    }
}

int aff3ct_turbo_encode(aff3ct_encoder_t handle, const int* U_K, int* X_N) {
    aff3ct_jl_clear_error();
    try {
        auto w = static_cast<TurboEncoderWrapper*>(handle);
        w->enc->encode(U_K, X_N);
        return 0;
    } catch (const std::exception& e) {
        aff3ct_jl_set_error(e.what());
        return -1;
    }
}

void aff3ct_turbo_encoder_destroy(aff3ct_encoder_t handle) {
    if (!handle) return;
    auto w = static_cast<TurboEncoderWrapper*>(handle);
    delete w->enc;
    delete w;
}

// ── Turbo decoder ───────────────────────────────────────────────────

aff3ct_decoder_t aff3ct_turbo_decoder_create(int K, int N, int n_ite,
                                              const char* interleaver_type,
                                              const int* poly, int poly_len,
                                              int buffered_encoding) {
    aff3ct_jl_clear_error();
    try {
        auto w = new TurboDecoderWrapper();

        auto p = get_poly(poly, poly_len);
        bool buffered = (buffered_encoding != 0);

        // Create a temporary RSC encoder to get the trellis
        int n_ff = 0;
        int v = p[0] > p[1] ? p[0] : p[1];
        while (v > 1) { n_ff++; v >>= 1; }
        int N_rsc = 2 * (K + n_ff);

        Encoder_RSC_generic_sys<int> tmp_enc(K, N_rsc, buffered, p);
        auto trellis = tmp_enc.get_trellis();

        w->siso_n = std::make_unique<Decoder_RSC_BCJR_seq_std<int, float>>(K, trellis, buffered);
        w->siso_i = std::make_unique<Decoder_RSC_BCJR_seq_std<int, float>>(K, trellis, buffered);

        w->itl_core = make_interleaver_core(interleaver_type, K);
        w->itl_core->refresh();
        w->itl = std::make_unique<Interleaver<float>>(*w->itl_core);

        w->dec = new Decoder_turbo_std<int, float>(
            K, N, n_ite, *w->siso_n, *w->siso_i, *w->itl, buffered);

        // Add LTE_VEC scaling factor (0.75× extrinsic) for proper convergence
        w->sf = std::make_unique<Scaling_factor_vec<int, float>>(n_ite);
        w->dec->add_post_processing(*w->sf);

        return static_cast<aff3ct_decoder_t>(w);
    } catch (const std::exception& e) {
        aff3ct_jl_set_error(e.what());
        return nullptr;
    }
}

int aff3ct_turbo_decode(aff3ct_decoder_t handle, const float* Y_N, int* V_K) {
    aff3ct_jl_clear_error();
    try {
        auto w = static_cast<TurboDecoderWrapper*>(handle);
        return w->dec->decode_siho(Y_N, V_K);
    } catch (const std::exception& e) {
        aff3ct_jl_set_error(e.what());
        return -1;
    }
}

void aff3ct_turbo_decoder_destroy(aff3ct_decoder_t handle) {
    if (!handle) return;
    auto w = static_cast<TurboDecoderWrapper*>(handle);
    delete w->dec;
    delete w;
}

} // extern "C"
