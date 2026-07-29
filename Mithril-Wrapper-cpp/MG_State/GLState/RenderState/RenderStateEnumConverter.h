// Mithril-Wrapper - MG_State/GLState/RenderState/RenderStateEnumConverter.h
//
// Bidirectional translation between raw GLenum values (as accepted/emitted by
// the OpenGL entry points) and the strongly-typed render-state enum classes
// declared in RenderStateEnum.h.
//
// Semantics (shared API contract):
//   * GLTo* are pure functions and never log. An unrecognised GLenum yields
//     `Xxx::Unknown`; it is the MG_Impl layer's responsibility to record
//     GL_INVALID_ENUM against the calling context.
//   * *ToGL map `Unknown` (and the `*Count` sentinel) to GL_NONE (0) so that a
//     partially-initialised / unknown enum round-trips to a safe no-op GL
//     value rather than a garbage GLenum.
#pragma once

#include <GL/gl.h>

#include "RenderStateEnum.h"

namespace mithril::glstate {

// ---- BlendFactor ----
BlendFactor GLToBlendFactor(GLenum v);
GLenum     BlendFactorToGL(BlendFactor v);

// ---- BlendEquation ----
BlendEquation GLToBlendEquation(GLenum v);
GLenum        BlendEquationToGL(BlendEquation v);

// ---- LogicOperation ----
LogicOperation GLToLogicOperation(GLenum v);
GLenum         LogicOperationToGL(LogicOperation v);

// ---- DepthTestFunc ----
DepthTestFunc GLToDepthTestFunc(GLenum v);
GLenum        DepthTestFuncToGL(DepthTestFunc v);

// ---- StencilOperation ----
StencilOperation GLToStencilOperation(GLenum v);
GLenum           StencilOperationToGL(StencilOperation v);

// ---- StencilFace ----
// Only GL_FRONT and GL_BACK map to a single face. GL_FRONT_AND_BACK is not
// representable here (callers split it into front+back before translation), so
// it falls through to StencilFace::Unknown.
StencilFace GLToStencilFace(GLenum v);
GLenum      StencilFaceToGL(StencilFace v);

// ---- CullFaceMode ----
CullFaceMode GLToCullFaceMode(GLenum v);
GLenum       CullFaceModeToGL(CullFaceMode v);

// ---- FrontFaceMode ----
FrontFaceMode GLToFrontFaceMode(GLenum v);
GLenum        FrontFaceModeToGL(FrontFaceMode v);

// ---- CapabilityInput ----
CapabilityInput GLToCapabilityInput(GLenum v);
GLenum          CapabilityInputToGL(CapabilityInput v);

// ---- PixelStoreParam ----
PixelStoreParam GLToPixelStoreParam(GLenum v);
GLenum          PixelStoreParamToGL(PixelStoreParam v);

} // namespace mithril::glstate
