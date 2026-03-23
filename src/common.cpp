#include "aff3ct_jl.h"
#include <string>

static thread_local std::string g_last_error;

// Internal: set last error message (called from other .cpp files)
void aff3ct_jl_set_error(const char* msg) {
    g_last_error = msg ? msg : "";
}

void aff3ct_jl_set_error(const std::string& msg) {
    g_last_error = msg;
}

void aff3ct_jl_clear_error() {
    g_last_error.clear();
}

extern "C" {

const char* aff3ct_jl_version(void) {
    return "0.1.0";
}

const char* aff3ct_jl_last_error(void) {
    if (g_last_error.empty()) return nullptr;
    return g_last_error.c_str();
}

} // extern "C"
