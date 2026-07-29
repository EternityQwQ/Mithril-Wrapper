// Mithril-Wrapper - MG_State/GLState/ErrorState/Error.cpp
//
// Implementation of ErrorState. See Error.h for the GL error-slot semantics
// (single slot, first-error retention until popped).
#include "Error.h"

#include <utility>

namespace mithril::glstate {

void ErrorState::RecordError(ErrorCode code, ErrorInfo info) {
    // GL semantics: the first recorded error is retained until glGetError
    // takes it; later errors are dropped. Mirrors the legacy state_set_error
    // "if (error == GL_NO_ERROR) error = err" path.
    if (m_glError != ErrorCode::NoError) {
        return;
    }
    m_glError = code;
    m_errorInfo = std::move(info);
}

bool ErrorState::HasGLError() const {
    return m_glError != ErrorCode::NoError;
}

ErrorCode ErrorState::PeekGLError() const {
    return m_glError;
}

ErrorCode ErrorState::PopGLError() {
    ErrorCode e = m_glError;
    m_glError = ErrorCode::NoError;
    m_errorInfo = {};
    return e;
}

void ErrorState::ClearErrors() {
    m_glError = ErrorCode::NoError;
    m_errorInfo = {};
}

GLenum ErrorState::ErrorCodeToGL(ErrorCode code) const {
    switch (code) {
        case ErrorCode::NoError:                     return GL_NO_ERROR;
        case ErrorCode::InvalidEnum:                 return GL_INVALID_ENUM;
        case ErrorCode::InvalidValue:                return GL_INVALID_VALUE;
        case ErrorCode::InvalidOperation:            return GL_INVALID_OPERATION;
        case ErrorCode::InvalidFramebufferOperation: return GL_INVALID_FRAMEBUFFER_OPERATION;
        case ErrorCode::OutOfMemory:                 return GL_OUT_OF_MEMORY;
        case ErrorCode::StackOverflow:               return GL_STACK_OVERFLOW;
        case ErrorCode::StackUnderflow:              return GL_STACK_UNDERFLOW;
        case ErrorCode::Unknown:                     return GL_NONE;
        default:                                     return GL_NONE;
    }
}

ErrorCode ErrorState::GLToErrorCode(GLenum gl) {
    switch (gl) {
        case GL_NO_ERROR:                     return ErrorCode::NoError;
        case GL_INVALID_ENUM:                 return ErrorCode::InvalidEnum;
        case GL_INVALID_VALUE:                return ErrorCode::InvalidValue;
        case GL_INVALID_OPERATION:            return ErrorCode::InvalidOperation;
        case GL_INVALID_FRAMEBUFFER_OPERATION: return ErrorCode::InvalidFramebufferOperation;
        case GL_OUT_OF_MEMORY:                return ErrorCode::OutOfMemory;
        case GL_STACK_OVERFLOW:               return ErrorCode::StackOverflow;
        case GL_STACK_UNDERFLOW:              return ErrorCode::StackUnderflow;
        default:                              return ErrorCode::Unknown;
    }
}

} // namespace mithril::glstate
