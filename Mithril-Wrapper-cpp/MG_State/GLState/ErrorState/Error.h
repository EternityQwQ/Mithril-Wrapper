// Mithril-Wrapper - MG_State/GLState/ErrorState/Error.h
//
// ErrorState: the modular replacement for the single `GLenum error` field that
// lived in the flat MG_State/State.h GLState. It owns the GL error slot used
// by glGetError and provides type-safe ErrorCode <-> GLenum translation.
//
// OpenGL error semantics (OpenGL 3.3 Core, §2.5): when a GL command detects
// an error, the implementation sets the current error code; subsequent errors
// are not recorded until glGetError retrieves (and resets) the current one.
// This implementation honours that contract with a single slot that retains
// the *first* recorded error until it is popped — matching the legacy
// state_set_error / state_take_error behaviour exactly.
#pragma once

#include <GL/gl.h>

#include "ErrorCode.h"
#include "ErrorInfo.h"

namespace mithril::glstate {

class ErrorState {
public:
    // Record `code` against the current GL context. Per GL semantics the first
    // recorded error is retained: if a non-NoError code is already pending,
    // the new code is dropped (and its `info` ignored). `info` carries
    // optional diagnostic context attached to the recorded error.
    void RecordError(ErrorCode code, ErrorInfo info = {});

    // True if a non-NoError error code is currently pending.
    bool HasGLError() const;

    // Inspect the pending error code without consuming it (NoError if none).
    ErrorCode PeekGLError() const;

    // Consume and return the pending error code; resets the slot to NoError.
    ErrorCode PopGLError();

    // Reset the error slot to NoError and discard any attached info.
    void ClearErrors();

    // Translate an ErrorCode to the GLenum returned by glGetError. Unknown
    // maps to GL_NONE (matching the RenderState translator convention).
    GLenum ErrorCodeToGL(ErrorCode code) const;

    // Reverse translation, used to keep the legacy state_set_error(GLenum)
    // path compatible with the new typed domain. An unrecognised GLenum
    // yields ErrorCode::Unknown.
    static ErrorCode GLToErrorCode(GLenum gl);

private:
    // Single GL error slot. Holds the first recorded error until popped.
    ErrorCode m_glError = ErrorCode::NoError;
    // Diagnostic payload attached to the currently pending error (if any).
    ErrorInfo m_errorInfo;
};

} // namespace mithril::glstate
