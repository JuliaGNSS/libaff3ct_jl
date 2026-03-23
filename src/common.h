#ifndef AFF3CT_JL_COMMON_H
#define AFF3CT_JL_COMMON_H

#include <string>

// Internal error helpers (not part of public API)
void aff3ct_jl_set_error(const char* msg);
void aff3ct_jl_set_error(const std::string& msg);
void aff3ct_jl_clear_error();

#endif
