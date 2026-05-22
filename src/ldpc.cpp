#include "aff3ct_jl.h"
#include "common.h"

#include <vector>
#include <string>
#include <stdexcept>

// aff3ct headers
#include <Module/Encoder/LDPC/From_H/Encoder_LDPC_from_H.hpp>
#include <Module/Decoder/LDPC/BP/Flooding/SPA/Decoder_LDPC_BP_flooding_SPA.hpp>
#include <Tools/Algo/Matrix/Sparse_matrix/Sparse_matrix.hpp>
#include <Tools/Code/LDPC/Matrix_handler/LDPC_matrix_handler.hpp>

using namespace aff3ct::module;
using namespace aff3ct::tools;

// ── Wrapper struct for sparse matrix + metadata ─────────────────────

struct SparseMatrixWrapper {
    Sparse_matrix                      mat;
    std::vector<uint32_t>              info_bits_pos;
};

// ── Wrapper struct for LDPC encoder ──────────────────────────────────

struct LDPCEncoderWrapper {
    Encoder_LDPC_from_H<int>*          enc;
};

extern "C" {

// ── Sparse matrix I/O ───────────────────────────────────────────────

aff3ct_sparse_matrix_t aff3ct_sparse_matrix_load(const char* filepath,
                                                  int* out_M, int* out_N,
                                                  unsigned int* info_bits_pos,
                                                  int info_bits_pos_len) {
    aff3ct_jl_clear_error();
    try {
        auto w = new SparseMatrixWrapper();
        w->mat = LDPC_matrix_handler::read(std::string(filepath), &w->info_bits_pos);

        // Report dimensions in standard LDPC convention: N = codeword
        int rows = (int)w->mat.get_n_rows();
        int cols = (int)w->mat.get_n_cols();
        int N = std::max(rows, cols);

        // If info_bits_pos wasn't populated by read(), compute it via identity transform
        if (w->info_bits_pos.empty()) {
            auto H_horiz = w->mat.turn(Sparse_matrix::Way::HORIZONTAL);
            LDPC_matrix_handler::transform_H_to_G_identity(H_horiz, w->info_bits_pos);
        }

        // M = effective parity checks (rank-based, from info_bits_pos)
        int M = N - (int)w->info_bits_pos.size();

        if (out_M) *out_M = M;
        if (out_N) *out_N = N;

        // Copy info_bits_pos to caller if requested
        if (info_bits_pos && info_bits_pos_len > 0) {
            int copy_len = (int)w->info_bits_pos.size();
            if (copy_len > info_bits_pos_len)
                copy_len = info_bits_pos_len;
            for (int i = 0; i < copy_len; i++)
                info_bits_pos[i] = w->info_bits_pos[i];
        }

        return static_cast<aff3ct_sparse_matrix_t>(w);
    } catch (const std::exception& e) {
        aff3ct_jl_set_error(e.what());
        return nullptr;
    }
}

int aff3ct_sparse_matrix_info_bits_count(aff3ct_sparse_matrix_t handle) {
    if (!handle) return 0;
    auto w = static_cast<SparseMatrixWrapper*>(handle);
    return (int)w->info_bits_pos.size();
}

int aff3ct_sparse_matrix_nrows(aff3ct_sparse_matrix_t handle) {
    if (!handle) return 0;
    auto w = static_cast<SparseMatrixWrapper*>(handle);
    return (int)w->mat.get_n_rows();
}

int aff3ct_sparse_matrix_ncols(aff3ct_sparse_matrix_t handle) {
    if (!handle) return 0;
    auto w = static_cast<SparseMatrixWrapper*>(handle);
    return (int)w->mat.get_n_cols();
}

void aff3ct_sparse_matrix_destroy(aff3ct_sparse_matrix_t handle) {
    delete static_cast<SparseMatrixWrapper*>(handle);
}

// ── LDPC encoder ────────────────────────────────────────────────────

aff3ct_encoder_t aff3ct_ldpc_encoder_create(int K, int N, aff3ct_sparse_matrix_t H_handle) {
    aff3ct_jl_clear_error();
    try {
        auto sm = static_cast<SparseMatrixWrapper*>(H_handle);
        auto H = sm->mat.turn(Sparse_matrix::Way::HORIZONTAL);

        auto w = new LDPCEncoderWrapper();
        // Use Encoder_LDPC_from_H with default "IDENTITY" method
        w->enc = new Encoder_LDPC_from_H<int>(K, N, H);
        return static_cast<aff3ct_encoder_t>(w);
    } catch (const std::exception& e) {
        aff3ct_jl_set_error(e.what());
        return nullptr;
    }
}

int aff3ct_ldpc_encode(aff3ct_encoder_t handle, const int* U_K, int* X_N) {
    aff3ct_jl_clear_error();
    try {
        auto w = static_cast<LDPCEncoderWrapper*>(handle);
        w->enc->encode(U_K, X_N);
        return 0;
    } catch (const std::exception& e) {
        aff3ct_jl_set_error(e.what());
        return -1;
    }
}

void aff3ct_ldpc_encoder_destroy(aff3ct_encoder_t handle) {
    if (!handle) return;
    auto w = static_cast<LDPCEncoderWrapper*>(handle);
    if (w->enc) delete w->enc;
    delete w;
}

// ── LDPC BP decoder ─────────────────────────────────────────────────

aff3ct_decoder_t aff3ct_ldpc_bp_decoder_create(int K, int N, int n_ite,
                                                aff3ct_sparse_matrix_t H_handle,
                                                const unsigned int* info_bits_pos) {
    aff3ct_jl_clear_error();
    try {
        auto sm = static_cast<SparseMatrixWrapper*>(H_handle);

        // Build info_bits_pos vector
        std::vector<uint32_t> ibp;
        if (info_bits_pos) {
            ibp.assign(info_bits_pos, info_bits_pos + K);
        } else {
            // Use positions from matrix load
            ibp = sm->info_bits_pos;
        }

        auto dec = new Decoder_LDPC_BP_flooding_SPA<int, float>(
            K, N, n_ite, sm->mat, ibp);
        return static_cast<aff3ct_decoder_t>(dec);
    } catch (const std::exception& e) {
        aff3ct_jl_set_error(e.what());
        return nullptr;
    }
}

int aff3ct_ldpc_bp_decode(aff3ct_decoder_t handle, const float* Y_N, int* V_K) {
    aff3ct_jl_clear_error();
    try {
        auto dec = static_cast<Decoder_LDPC_BP_flooding_SPA<int, float>*>(handle);
        return dec->decode_siho(Y_N, V_K);
    } catch (const std::exception& e) {
        aff3ct_jl_set_error(e.what());
        return -1;
    }
}

void aff3ct_ldpc_bp_decoder_destroy(aff3ct_decoder_t handle) {
    delete static_cast<Decoder_LDPC_BP_flooding_SPA<int, float>*>(handle);
}

} // extern "C"
