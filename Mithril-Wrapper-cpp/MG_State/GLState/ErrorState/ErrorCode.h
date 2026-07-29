// Mithril-Wrapper - MG_State/GLState/ErrorState/ErrorCode.h
//
// Strongly-typed enum class for the OpenGL error-code domain. Replaces the
// bare `GLenum error` field that lived in the flat MG_State/State.h GLState,
// giving the new modular state machine a type-safe representation of the
// glGetError result.
//
// Each enumerator maps 1:1 to a standard OpenGL error constant; the matching
// GL_* macro is noted in the trailing comment (OpenGL 3.3 Core, §2.5 GL
// Errors). This header is intentionally GL-agnostic (no <GL/gl.h> dependency):
// it only defines the value domain. The ErrorCode <-> GLenum translation lives
// in Error.h/.cpp.
#pragma once

namespace mithril::glstate {

enum class ErrorCode {
    NoError,                     // GL_NO_ERROR
    InvalidEnum,                 // GL_INVALID_ENUM
    InvalidValue,                // GL_INVALID_VALUE
    InvalidOperation,            // GL_INVALID_OPERATION
    InvalidFramebufferOperation, // GL_INVALID_FRAMEBUFFER_OPERATION
    OutOfMemory,                 // GL_OUT_OF_MEMORY
    StackOverflow,               // GL_STACK_OVERFLOW
    StackUnderflow,              // GL_STACK_UNDERFLOW
    Unknown                      // Result of translating an unrecognised GLenum.
};

} // namespace mithril::glstate
