#include "aff3ct_jl.h"
#include "common.h"

#include <vector>
#include <memory>
#include <cmath>
#include <stdexcept>

// aff3ct headers
#include <Module/Encoder/Polar/Encoder_polar_sys.hpp>
#include <Module/Decoder/Polar/SC/Decoder_polar_SC_fast_sys.hpp>
#include <Module/Decoder/Polar/SCL/Decoder_polar_SCL_MEM_fast_sys.hpp>
#include <Tools/Code/Polar/Frozenbits_generator/Frozenbits_generator_GA.hpp>
#include <Tools/Code/Polar/Frozenbits_generator/Frozenbits_generator_5G.hpp>
#include <Tools/Code/Polar/Polar_code.hpp>
#include <Tools/Noise/Sigma.hpp>

using namespace aff3ct::module;
using namespace aff3ct::tools;

// ── helpers ──────────────────────────────────────────────────────────

static std::vector<bool> ints_to_bools(const int* arr, int n) {
    std::vector<bool> v(n);
    for (int i = 0; i < n; i++) v[i] = (arr[i] != 0);
    return v;
}

// ── Frozen bits generators ──────────────────────────────────────────

struct FBGenWrapper {
    Frozenbits_generator*            gen;
    std::unique_ptr<Sigma<float>>    sigma;
    Polar_code*                      code;  // must outlive gen (GA holds a ref)
    int N;
};

extern "C" {

aff3ct_frozenbits_gen_t aff3ct_frozenbits_gen_ga_create(int K, int N, float design_snr) {
    aff3ct_jl_clear_error();
    try {
        // Standard Arikan 2x2 kernel: [[1,0],[1,1]]
        std::vector<std::vector<bool>> kernel = {{true, false}, {true, true}};
        auto code = new Polar_code(N, kernel);

        auto gen = new Frozenbits_generator_GA(K, N, *code);

        // Convert design Eb/N0 (dB) to sigma
        // sigma = 1 / sqrt(2 * R * 10^(EbN0_dB/10)), R = K/N
        float R = (float)K / (float)N;
        float ebn0_lin = std::pow(10.0f, design_snr / 10.0f);
        float esn0_lin = ebn0_lin * R;
        float sigma_val = 1.0f / std::sqrt(2.0f * esn0_lin);

        auto w = new FBGenWrapper();
        w->gen = gen;
        w->code = code;  // keep alive — GA holds a const ref
        w->sigma = std::make_unique<Sigma<float>>(sigma_val, design_snr, 10.0f * std::log10(esn0_lin));
        w->N = N;
        gen->set_noise(*w->sigma);

        return static_cast<aff3ct_frozenbits_gen_t>(w);
    } catch (const std::exception& e) {
        aff3ct_jl_set_error(e.what());
        return nullptr;
    }
}

aff3ct_frozenbits_gen_t aff3ct_frozenbits_gen_5g_create(int K, int N) {
    aff3ct_jl_clear_error();
    try {
        auto gen = new Frozenbits_generator_5G(K, N);
        auto w = new FBGenWrapper();
        w->gen = gen;
        w->code = nullptr;
        w->N = N;
        // 5G generator doesn't need noise, but set_noise is required before generate
        w->sigma = std::make_unique<Sigma<float>>(1.0f, 0.0f, 0.0f);
        gen->set_noise(*w->sigma);
        return static_cast<aff3ct_frozenbits_gen_t>(w);
    } catch (const std::exception& e) {
        aff3ct_jl_set_error(e.what());
        return nullptr;
    }
}

int aff3ct_frozenbits_gen_generate(aff3ct_frozenbits_gen_t handle, int* frozen_bits) {
    aff3ct_jl_clear_error();
    try {
        auto w = static_cast<FBGenWrapper*>(handle);
        std::vector<bool> fb(w->N);
        w->gen->generate(fb);
        for (int i = 0; i < w->N; i++)
            frozen_bits[i] = fb[i] ? 1 : 0;
        return 0;
    } catch (const std::exception& e) {
        aff3ct_jl_set_error(e.what());
        return -1;
    }
}

void aff3ct_frozenbits_gen_destroy(aff3ct_frozenbits_gen_t handle) {
    if (!handle) return;
    auto w = static_cast<FBGenWrapper*>(handle);
    delete w->gen;
    delete w->code;
    delete w;
}

// ── Polar encoder ───────────────────────────────────────────────────

aff3ct_encoder_t aff3ct_polar_encoder_create(int K, int N, const int* frozen_bits) {
    aff3ct_jl_clear_error();
    try {
        auto fb = ints_to_bools(frozen_bits, N);
        auto enc = new Encoder_polar_sys<int>(K, N, fb);
        return static_cast<aff3ct_encoder_t>(enc);
    } catch (const std::exception& e) {
        aff3ct_jl_set_error(e.what());
        return nullptr;
    }
}

int aff3ct_polar_encode(aff3ct_encoder_t handle, const int* U_K, int* X_N) {
    aff3ct_jl_clear_error();
    try {
        auto enc = static_cast<Encoder_polar_sys<int>*>(handle);
        enc->encode(U_K, X_N);
        return 0;
    } catch (const std::exception& e) {
        aff3ct_jl_set_error(e.what());
        return -1;
    }
}

void aff3ct_polar_encoder_destroy(aff3ct_encoder_t handle) {
    delete static_cast<Encoder_polar_sys<int>*>(handle);
}

// ── Polar SC decoder ────────────────────────────────────────────────

aff3ct_decoder_t aff3ct_polar_sc_decoder_create(int K, int N, const int* frozen_bits) {
    aff3ct_jl_clear_error();
    try {
        auto fb = ints_to_bools(frozen_bits, N);
        auto dec = new Decoder_polar_SC_fast_sys<int, float>(K, N, fb);
        return static_cast<aff3ct_decoder_t>(dec);
    } catch (const std::exception& e) {
        aff3ct_jl_set_error(e.what());
        return nullptr;
    }
}

int aff3ct_polar_sc_decode(aff3ct_decoder_t handle, const float* Y_N, int* V_K) {
    aff3ct_jl_clear_error();
    try {
        auto dec = static_cast<Decoder_polar_SC_fast_sys<int, float>*>(handle);
        int status = dec->decode_siho(Y_N, V_K);
        // Fast decoder uses bit-packed format: bit=1 → 0x80000000, bit=0 → 0
        // Convert to standard 0/1
        int K = dec->get_K();
        for (int i = 0; i < K; i++)
            V_K[i] = (V_K[i] != 0) ? 1 : 0;
        return status;
    } catch (const std::exception& e) {
        aff3ct_jl_set_error(e.what());
        return -1;
    }
}

void aff3ct_polar_sc_decoder_destroy(aff3ct_decoder_t handle) {
    delete static_cast<Decoder_polar_SC_fast_sys<int, float>*>(handle);
}

// ── Polar SCL decoder ───────────────────────────────────────────────

aff3ct_decoder_t aff3ct_polar_scl_decoder_create(int K, int N, int L,
                                                   const int* frozen_bits) {
    aff3ct_jl_clear_error();
    try {
        auto fb = ints_to_bools(frozen_bits, N);
        auto dec = new Decoder_polar_SCL_MEM_fast_sys<int, float>(K, N, L, fb);
        return static_cast<aff3ct_decoder_t>(dec);
    } catch (const std::exception& e) {
        aff3ct_jl_set_error(e.what());
        return nullptr;
    }
}

int aff3ct_polar_scl_decode(aff3ct_decoder_t handle, const float* Y_N, int* V_K) {
    aff3ct_jl_clear_error();
    try {
        auto dec = static_cast<Decoder_polar_SCL_MEM_fast_sys<int, float>*>(handle);
        int status = dec->decode_siho(Y_N, V_K);
        // Fast decoder uses bit-packed format: bit=1 → 0x80000000, bit=0 → 0
        int K = dec->get_K();
        for (int i = 0; i < K; i++)
            V_K[i] = (V_K[i] != 0) ? 1 : 0;
        return status;
    } catch (const std::exception& e) {
        aff3ct_jl_set_error(e.what());
        return -1;
    }
}

void aff3ct_polar_scl_decoder_destroy(aff3ct_decoder_t handle) {
    delete static_cast<Decoder_polar_SCL_MEM_fast_sys<int, float>*>(handle);
}

} // extern "C"
