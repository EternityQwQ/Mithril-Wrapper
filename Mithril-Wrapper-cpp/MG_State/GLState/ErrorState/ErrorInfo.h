// Mithril-Wrapper - MG_State/GLState/ErrorState/ErrorInfo.h
//
// Optional diagnostic payload attached to a recorded ErrorCode. Carries the
// human-readable message and the source location that produced the error, so
// the MG_Impl layer can surface actionable context (e.g. via GL_KHR_debug)
// beyond the bare GLenum that glGetError returns.
#pragma once

#include <string>

namespace mithril::glstate {

struct ErrorInfo {
    std::string message;
    int line = 0;
    const char* file = nullptr;
};

} // namespace mithril::glstate
