// Mithril-Wrapper - MG_State/GLState/RenderState/RenderStateEnumConverter.cpp
//
// Implementation of the GL <-> render-state-enum translators declared in
// RenderStateEnumConverter.h. See that header for the shared semantics
// (Unknown on unrecognised GLenum; GL_NONE on Unknown / *Count).
#include "RenderStateEnumConverter.h"

namespace mithril::glstate {

/*
 * The project ships a minimal glcorearb.h that omits a number of standard
 * OpenGL Core Profile constants which the render-state surface still needs
 * (the full glLogicOp operand set, the byte-order / 3D-image pixel-store
 * pnames, and a handful of glEnable targets). They are standard, stable GL
 * values; define them here under #ifndef guards so this translation unit is
 * self-contained without modifying the shared GL headers. This mirrors the
 * existing convention in include/GL/gl.h (GL_PROXY_TEXTURE_2D et al.).
 */

// ---- glLogicOp operands (GL 1.0 LogicOp enum, 0x1500..0x150F) ----
#ifndef GL_CLEAR
#define GL_CLEAR                        0x1500
#endif
#ifndef GL_AND
#define GL_AND                          0x1501
#endif
#ifndef GL_AND_REVERSE
#define GL_AND_REVERSE                  0x1502
#endif
#ifndef GL_COPY
#define GL_COPY                         0x1503
#endif
#ifndef GL_AND_INVERTED
#define GL_AND_INVERTED                 0x1504
#endif
#ifndef GL_NOOP
#define GL_NOOP                         0x1505
#endif
#ifndef GL_XOR
#define GL_XOR                          0x1506
#endif
#ifndef GL_OR
#define GL_OR                           0x1507
#endif
#ifndef GL_NOR
#define GL_NOR                          0x1508
#endif
#ifndef GL_EQUIV
#define GL_EQUIV                        0x1509
#endif
#ifndef GL_OR_REVERSE
#define GL_OR_REVERSE                   0x150B
#endif
#ifndef GL_COPY_INVERTED
#define GL_COPY_INVERTED                0x150C
#endif
#ifndef GL_OR_INVERTED
#define GL_OR_INVERTED                  0x150D
#endif
#ifndef GL_NAND
#define GL_NAND                         0x150E
#endif
#ifndef GL_SET
#define GL_SET                          0x150F
#endif

// ---- glPixelStorei byte-order / 3D-image pnames ----
#ifndef GL_UNPACK_SWAP_BYTES
#define GL_UNPACK_SWAP_BYTES            0x0CF0
#endif
#ifndef GL_UNPACK_LSB_FIRST
#define GL_UNPACK_LSB_FIRST             0x0CF1
#endif
#ifndef GL_PACK_SWAP_BYTES
#define GL_PACK_SWAP_BYTES              0x0D00
#endif
#ifndef GL_PACK_LSB_FIRST
#define GL_PACK_LSB_FIRST               0x0D01
#endif
#ifndef GL_PACK_SKIP_IMAGES
#define GL_PACK_SKIP_IMAGES             0x806B
#endif
#ifndef GL_PACK_IMAGE_HEIGHT
#define GL_PACK_IMAGE_HEIGHT            0x806C
#endif

// ---- glEnable / glDisable targets missing from the minimal header ----
#ifndef GL_CLIP_DISTANCE0
#define GL_CLIP_DISTANCE0               0x3000
#endif
#ifndef GL_CLIP_DISTANCE1
#define GL_CLIP_DISTANCE1               0x3001
#endif
#ifndef GL_CLIP_DISTANCE2
#define GL_CLIP_DISTANCE2               0x3002
#endif
#ifndef GL_CLIP_DISTANCE3
#define GL_CLIP_DISTANCE3               0x3003
#endif
#ifndef GL_CLIP_DISTANCE4
#define GL_CLIP_DISTANCE4               0x3004
#endif
#ifndef GL_CLIP_DISTANCE5
#define GL_CLIP_DISTANCE5               0x3005
#endif
#ifndef GL_CLIP_DISTANCE6
#define GL_CLIP_DISTANCE6               0x3006
#endif
#ifndef GL_CLIP_DISTANCE7
#define GL_CLIP_DISTANCE7               0x3007
#endif
#ifndef GL_COLOR_LOGIC_OP
#define GL_COLOR_LOGIC_OP               0x0BF2
#endif
#ifndef GL_DEPTH_CLAMP
#define GL_DEPTH_CLAMP                  0x864F
#endif
#ifndef GL_POLYGON_SMOOTH
#define GL_POLYGON_SMOOTH               0x0B41
#endif
#ifndef GL_SAMPLE_SHADING
#define GL_SAMPLE_SHADING               0x8C36
#endif
#ifndef GL_SAMPLE_MASK
#define GL_SAMPLE_MASK                  0x8E51
#endif

// ===========================================================================
// BlendFactor
// ===========================================================================
BlendFactor GLToBlendFactor(GLenum v) {
    switch (v) {
        case GL_ZERO: return BlendFactor::Zero;
        case GL_ONE: return BlendFactor::One;
        case GL_SRC_COLOR: return BlendFactor::SrcColor;
        case GL_ONE_MINUS_SRC_COLOR: return BlendFactor::OneMinusSrcColor;
        case GL_DST_COLOR: return BlendFactor::DstColor;
        case GL_ONE_MINUS_DST_COLOR: return BlendFactor::OneMinusDstColor;
        case GL_SRC_ALPHA: return BlendFactor::SrcAlpha;
        case GL_ONE_MINUS_SRC_ALPHA: return BlendFactor::OneMinusSrcAlpha;
        case GL_DST_ALPHA: return BlendFactor::DstAlpha;
        case GL_ONE_MINUS_DST_ALPHA: return BlendFactor::OneMinusDstAlpha;
        case GL_CONSTANT_COLOR: return BlendFactor::ConstantColor;
        case GL_ONE_MINUS_CONSTANT_COLOR: return BlendFactor::OneMinusConstantColor;
        case GL_CONSTANT_ALPHA: return BlendFactor::ConstantAlpha;
        case GL_ONE_MINUS_CONSTANT_ALPHA: return BlendFactor::OneMinusConstantAlpha;
        case GL_SRC1_COLOR: return BlendFactor::Src1Color;
        case GL_ONE_MINUS_SRC1_COLOR: return BlendFactor::OneMinusSrc1Color;
        case GL_SRC1_ALPHA: return BlendFactor::Src1Alpha;
        case GL_ONE_MINUS_SRC1_ALPHA: return BlendFactor::OneMinusSrc1Alpha;
        default: return BlendFactor::Unknown;
    }
}

GLenum BlendFactorToGL(BlendFactor v) {
    switch (v) {
        case BlendFactor::Zero: return GL_ZERO;
        case BlendFactor::One: return GL_ONE;
        case BlendFactor::SrcColor: return GL_SRC_COLOR;
        case BlendFactor::OneMinusSrcColor: return GL_ONE_MINUS_SRC_COLOR;
        case BlendFactor::DstColor: return GL_DST_COLOR;
        case BlendFactor::OneMinusDstColor: return GL_ONE_MINUS_DST_COLOR;
        case BlendFactor::SrcAlpha: return GL_SRC_ALPHA;
        case BlendFactor::OneMinusSrcAlpha: return GL_ONE_MINUS_SRC_ALPHA;
        case BlendFactor::DstAlpha: return GL_DST_ALPHA;
        case BlendFactor::OneMinusDstAlpha: return GL_ONE_MINUS_DST_ALPHA;
        case BlendFactor::ConstantColor: return GL_CONSTANT_COLOR;
        case BlendFactor::OneMinusConstantColor: return GL_ONE_MINUS_CONSTANT_COLOR;
        case BlendFactor::ConstantAlpha: return GL_CONSTANT_ALPHA;
        case BlendFactor::OneMinusConstantAlpha: return GL_ONE_MINUS_CONSTANT_ALPHA;
        case BlendFactor::Src1Color: return GL_SRC1_COLOR;
        case BlendFactor::OneMinusSrc1Color: return GL_ONE_MINUS_SRC1_COLOR;
        case BlendFactor::Src1Alpha: return GL_SRC1_ALPHA;
        case BlendFactor::OneMinusSrc1Alpha: return GL_ONE_MINUS_SRC1_ALPHA;
        case BlendFactor::BlendFactorCount: return GL_NONE;
        case BlendFactor::Unknown: return GL_NONE;
        default: return GL_NONE;
    }
}

// ===========================================================================
// BlendEquation
// ===========================================================================
BlendEquation GLToBlendEquation(GLenum v) {
    switch (v) {
        case GL_FUNC_ADD: return BlendEquation::Add;
        case GL_FUNC_SUBTRACT: return BlendEquation::Subtract;
        case GL_FUNC_REVERSE_SUBTRACT: return BlendEquation::ReverseSubtract;
        case GL_MIN: return BlendEquation::Min;
        case GL_MAX: return BlendEquation::Max;
        default: return BlendEquation::Unknown;
    }
}

GLenum BlendEquationToGL(BlendEquation v) {
    switch (v) {
        case BlendEquation::Add: return GL_FUNC_ADD;
        case BlendEquation::Subtract: return GL_FUNC_SUBTRACT;
        case BlendEquation::ReverseSubtract: return GL_FUNC_REVERSE_SUBTRACT;
        case BlendEquation::Min: return GL_MIN;
        case BlendEquation::Max: return GL_MAX;
        case BlendEquation::BlendEquationCount: return GL_NONE;
        case BlendEquation::Unknown: return GL_NONE;
        default: return GL_NONE;
    }
}

// ===========================================================================
// LogicOperation
// ===========================================================================
LogicOperation GLToLogicOperation(GLenum v) {
    switch (v) {
        case GL_CLEAR: return LogicOperation::Clear;
        case GL_AND: return LogicOperation::And;
        case GL_AND_REVERSE: return LogicOperation::AndReverse;
        case GL_COPY: return LogicOperation::Copy;
        case GL_AND_INVERTED: return LogicOperation::AndInverted;
        case GL_NOOP: return LogicOperation::Noop;
        case GL_XOR: return LogicOperation::Xor;
        case GL_OR: return LogicOperation::Or;
        case GL_NOR: return LogicOperation::Nor;
        case GL_EQUIV: return LogicOperation::Equiv;
        case GL_INVERT: return LogicOperation::Invert;
        case GL_OR_REVERSE: return LogicOperation::OrReverse;
        case GL_COPY_INVERTED: return LogicOperation::CopyInverted;
        case GL_OR_INVERTED: return LogicOperation::OrInverted;
        case GL_NAND: return LogicOperation::Nand;
        case GL_SET: return LogicOperation::Set;
        default: return LogicOperation::Unknown;
    }
}

GLenum LogicOperationToGL(LogicOperation v) {
    switch (v) {
        case LogicOperation::Clear: return GL_CLEAR;
        case LogicOperation::And: return GL_AND;
        case LogicOperation::AndReverse: return GL_AND_REVERSE;
        case LogicOperation::Copy: return GL_COPY;
        case LogicOperation::AndInverted: return GL_AND_INVERTED;
        case LogicOperation::Noop: return GL_NOOP;
        case LogicOperation::Xor: return GL_XOR;
        case LogicOperation::Or: return GL_OR;
        case LogicOperation::Nor: return GL_NOR;
        case LogicOperation::Equiv: return GL_EQUIV;
        case LogicOperation::Invert: return GL_INVERT;
        case LogicOperation::OrReverse: return GL_OR_REVERSE;
        case LogicOperation::CopyInverted: return GL_COPY_INVERTED;
        case LogicOperation::OrInverted: return GL_OR_INVERTED;
        case LogicOperation::Nand: return GL_NAND;
        case LogicOperation::Set: return GL_SET;
        case LogicOperation::LogicOperationCount: return GL_NONE;
        case LogicOperation::Unknown: return GL_NONE;
        default: return GL_NONE;
    }
}

// ===========================================================================
// DepthTestFunc
// ===========================================================================
DepthTestFunc GLToDepthTestFunc(GLenum v) {
    switch (v) {
        case GL_NEVER: return DepthTestFunc::Never;
        case GL_LESS: return DepthTestFunc::Less;
        case GL_EQUAL: return DepthTestFunc::Equal;
        case GL_LEQUAL: return DepthTestFunc::LessEqual;
        case GL_GREATER: return DepthTestFunc::Greater;
        case GL_NOTEQUAL: return DepthTestFunc::NotEqual;
        case GL_GEQUAL: return DepthTestFunc::GreaterEqual;
        case GL_ALWAYS: return DepthTestFunc::Always;
        default: return DepthTestFunc::Unknown;
    }
}

GLenum DepthTestFuncToGL(DepthTestFunc v) {
    switch (v) {
        case DepthTestFunc::Never: return GL_NEVER;
        case DepthTestFunc::Less: return GL_LESS;
        case DepthTestFunc::Equal: return GL_EQUAL;
        case DepthTestFunc::LessEqual: return GL_LEQUAL;
        case DepthTestFunc::Greater: return GL_GREATER;
        case DepthTestFunc::NotEqual: return GL_NOTEQUAL;
        case DepthTestFunc::GreaterEqual: return GL_GEQUAL;
        case DepthTestFunc::Always: return GL_ALWAYS;
        case DepthTestFunc::DepthTestFuncCount: return GL_NONE;
        case DepthTestFunc::Unknown: return GL_NONE;
        default: return GL_NONE;
    }
}

// ===========================================================================
// StencilOperation
// ===========================================================================
StencilOperation GLToStencilOperation(GLenum v) {
    switch (v) {
        case GL_KEEP: return StencilOperation::Keep;
        case GL_ZERO: return StencilOperation::Zero;
        case GL_REPLACE: return StencilOperation::Replace;
        case GL_INCR: return StencilOperation::IncrementClamp;
        case GL_DECR: return StencilOperation::DecrementClamp;
        case GL_INVERT: return StencilOperation::Invert;
        case GL_INCR_WRAP: return StencilOperation::IncrementWrap;
        case GL_DECR_WRAP: return StencilOperation::DecrementWrap;
        default: return StencilOperation::Unknown;
    }
}

GLenum StencilOperationToGL(StencilOperation v) {
    switch (v) {
        case StencilOperation::Keep: return GL_KEEP;
        case StencilOperation::Zero: return GL_ZERO;
        case StencilOperation::Replace: return GL_REPLACE;
        case StencilOperation::IncrementClamp: return GL_INCR;
        case StencilOperation::DecrementClamp: return GL_DECR;
        case StencilOperation::Invert: return GL_INVERT;
        case StencilOperation::IncrementWrap: return GL_INCR_WRAP;
        case StencilOperation::DecrementWrap: return GL_DECR_WRAP;
        case StencilOperation::StencilOperationCount: return GL_NONE;
        case StencilOperation::Unknown: return GL_NONE;
        default: return GL_NONE;
    }
}

// ===========================================================================
// StencilFace
// ===========================================================================
StencilFace GLToStencilFace(GLenum v) {
    switch (v) {
        case GL_FRONT: return StencilFace::Front;
        case GL_BACK: return StencilFace::Back;
        // GL_FRONT_AND_BACK is intentionally not mapped to a single face; the
        // caller splits it into front+back before reaching this translator.
        default: return StencilFace::Unknown;
    }
}

GLenum StencilFaceToGL(StencilFace v) {
    switch (v) {
        case StencilFace::Front: return GL_FRONT;
        case StencilFace::Back: return GL_BACK;
        case StencilFace::StencilFaceCount: return GL_NONE;
        case StencilFace::Unknown: return GL_NONE;
        default: return GL_NONE;
    }
}

// ===========================================================================
// CullFaceMode
// ===========================================================================
CullFaceMode GLToCullFaceMode(GLenum v) {
    switch (v) {
        case GL_FRONT: return CullFaceMode::Front;
        case GL_BACK: return CullFaceMode::Back;
        case GL_FRONT_AND_BACK: return CullFaceMode::FrontAndBack;
        default: return CullFaceMode::Unknown;
    }
}

GLenum CullFaceModeToGL(CullFaceMode v) {
    switch (v) {
        case CullFaceMode::Front: return GL_FRONT;
        case CullFaceMode::Back: return GL_BACK;
        case CullFaceMode::FrontAndBack: return GL_FRONT_AND_BACK;
        case CullFaceMode::CullFaceModeCount: return GL_NONE;
        case CullFaceMode::Unknown: return GL_NONE;
        default: return GL_NONE;
    }
}

// ===========================================================================
// FrontFaceMode
// ===========================================================================
FrontFaceMode GLToFrontFaceMode(GLenum v) {
    switch (v) {
        case GL_CCW: return FrontFaceMode::CounterClockwise;
        case GL_CW: return FrontFaceMode::Clockwise;
        default: return FrontFaceMode::Unknown;
    }
}

GLenum FrontFaceModeToGL(FrontFaceMode v) {
    switch (v) {
        case FrontFaceMode::CounterClockwise: return GL_CCW;
        case FrontFaceMode::Clockwise: return GL_CW;
        case FrontFaceMode::FrontFaceModeCount: return GL_NONE;
        case FrontFaceMode::Unknown: return GL_NONE;
        default: return GL_NONE;
    }
}

// ===========================================================================
// CapabilityInput
// ===========================================================================
CapabilityInput GLToCapabilityInput(GLenum v) {
    switch (v) {
        case GL_BLEND: return CapabilityInput::Blend;
        case GL_CLIP_DISTANCE0: return CapabilityInput::ClipDistance0;
        case GL_CLIP_DISTANCE1: return CapabilityInput::ClipDistance1;
        case GL_CLIP_DISTANCE2: return CapabilityInput::ClipDistance2;
        case GL_CLIP_DISTANCE3: return CapabilityInput::ClipDistance3;
        case GL_CLIP_DISTANCE4: return CapabilityInput::ClipDistance4;
        case GL_CLIP_DISTANCE5: return CapabilityInput::ClipDistance5;
        case GL_CLIP_DISTANCE6: return CapabilityInput::ClipDistance6;
        case GL_CLIP_DISTANCE7: return CapabilityInput::ClipDistance7;
        case GL_COLOR_LOGIC_OP: return CapabilityInput::ColorLogicOp;
        case GL_CULL_FACE: return CapabilityInput::CullFace;
        case GL_DEBUG_OUTPUT: return CapabilityInput::DebugOutput;
        case GL_DEBUG_OUTPUT_SYNCHRONOUS: return CapabilityInput::DebugOutputSynchronous;
        case GL_DEPTH_CLAMP: return CapabilityInput::DepthClamp;
        case GL_DEPTH_TEST: return CapabilityInput::DepthTest;
        case GL_DITHER: return CapabilityInput::Dither;
        case GL_FRAMEBUFFER_SRGB: return CapabilityInput::FramebufferSrgb;
        case GL_LINE_SMOOTH: return CapabilityInput::LineSmooth;
        case GL_MULTISAMPLE: return CapabilityInput::Multisample;
        case GL_POLYGON_OFFSET_FILL: return CapabilityInput::PolygonOffsetFill;
        case GL_POLYGON_OFFSET_LINE: return CapabilityInput::PolygonOffsetLine;
        case GL_POLYGON_OFFSET_POINT: return CapabilityInput::PolygonOffsetPoint;
        case GL_POLYGON_SMOOTH: return CapabilityInput::PolygonSmooth;
        case GL_PRIMITIVE_RESTART: return CapabilityInput::PrimitiveRestart;
        case GL_PRIMITIVE_RESTART_FIXED_INDEX: return CapabilityInput::PrimitiveRestartFixedIndex;
        case GL_RASTERIZER_DISCARD: return CapabilityInput::RasterizerDiscard;
        case GL_SAMPLE_ALPHA_TO_COVERAGE: return CapabilityInput::SampleAlphaToCoverage;
        case GL_SAMPLE_ALPHA_TO_ONE: return CapabilityInput::SampleAlphaToOne;
        case GL_SAMPLE_COVERAGE: return CapabilityInput::SampleCoverage;
        case GL_SAMPLE_SHADING: return CapabilityInput::SampleShading;
        case GL_SAMPLE_MASK: return CapabilityInput::SampleMask;
        case GL_SCISSOR_TEST: return CapabilityInput::ScissorTest;
        case GL_STENCIL_TEST: return CapabilityInput::StencilTest;
        case GL_TEXTURE_CUBE_MAP_SEAMLESS: return CapabilityInput::TextureCubeMapSeamless;
        case GL_PROGRAM_POINT_SIZE: return CapabilityInput::ProgramPointSize;
        default: return CapabilityInput::Unknown;
    }
}

GLenum CapabilityInputToGL(CapabilityInput v) {
    switch (v) {
        case CapabilityInput::Blend: return GL_BLEND;
        case CapabilityInput::ClipDistance0: return GL_CLIP_DISTANCE0;
        case CapabilityInput::ClipDistance1: return GL_CLIP_DISTANCE1;
        case CapabilityInput::ClipDistance2: return GL_CLIP_DISTANCE2;
        case CapabilityInput::ClipDistance3: return GL_CLIP_DISTANCE3;
        case CapabilityInput::ClipDistance4: return GL_CLIP_DISTANCE4;
        case CapabilityInput::ClipDistance5: return GL_CLIP_DISTANCE5;
        case CapabilityInput::ClipDistance6: return GL_CLIP_DISTANCE6;
        case CapabilityInput::ClipDistance7: return GL_CLIP_DISTANCE7;
        case CapabilityInput::ColorLogicOp: return GL_COLOR_LOGIC_OP;
        case CapabilityInput::CullFace: return GL_CULL_FACE;
        case CapabilityInput::DebugOutput: return GL_DEBUG_OUTPUT;
        case CapabilityInput::DebugOutputSynchronous: return GL_DEBUG_OUTPUT_SYNCHRONOUS;
        case CapabilityInput::DepthClamp: return GL_DEPTH_CLAMP;
        case CapabilityInput::DepthTest: return GL_DEPTH_TEST;
        case CapabilityInput::Dither: return GL_DITHER;
        case CapabilityInput::FramebufferSrgb: return GL_FRAMEBUFFER_SRGB;
        case CapabilityInput::LineSmooth: return GL_LINE_SMOOTH;
        case CapabilityInput::Multisample: return GL_MULTISAMPLE;
        case CapabilityInput::PolygonOffsetFill: return GL_POLYGON_OFFSET_FILL;
        case CapabilityInput::PolygonOffsetLine: return GL_POLYGON_OFFSET_LINE;
        case CapabilityInput::PolygonOffsetPoint: return GL_POLYGON_OFFSET_POINT;
        case CapabilityInput::PolygonSmooth: return GL_POLYGON_SMOOTH;
        case CapabilityInput::PrimitiveRestart: return GL_PRIMITIVE_RESTART;
        case CapabilityInput::PrimitiveRestartFixedIndex: return GL_PRIMITIVE_RESTART_FIXED_INDEX;
        case CapabilityInput::RasterizerDiscard: return GL_RASTERIZER_DISCARD;
        case CapabilityInput::SampleAlphaToCoverage: return GL_SAMPLE_ALPHA_TO_COVERAGE;
        case CapabilityInput::SampleAlphaToOne: return GL_SAMPLE_ALPHA_TO_ONE;
        case CapabilityInput::SampleCoverage: return GL_SAMPLE_COVERAGE;
        case CapabilityInput::SampleShading: return GL_SAMPLE_SHADING;
        case CapabilityInput::SampleMask: return GL_SAMPLE_MASK;
        case CapabilityInput::ScissorTest: return GL_SCISSOR_TEST;
        case CapabilityInput::StencilTest: return GL_STENCIL_TEST;
        case CapabilityInput::TextureCubeMapSeamless: return GL_TEXTURE_CUBE_MAP_SEAMLESS;
        case CapabilityInput::ProgramPointSize: return GL_PROGRAM_POINT_SIZE;
        case CapabilityInput::CapabilityInputCount: return GL_NONE;
        case CapabilityInput::Unknown: return GL_NONE;
        default: return GL_NONE;
    }
}

// ===========================================================================
// PixelStoreParam
// ===========================================================================
PixelStoreParam GLToPixelStoreParam(GLenum v) {
    switch (v) {
        case GL_PACK_ALIGNMENT: return PixelStoreParam::PackAlignment;
        case GL_PACK_ROW_LENGTH: return PixelStoreParam::PackRowLength;
        case GL_PACK_IMAGE_HEIGHT: return PixelStoreParam::PackImageHeight;
        case GL_PACK_SKIP_ROWS: return PixelStoreParam::PackSkipRows;
        case GL_PACK_SKIP_PIXELS: return PixelStoreParam::PackSkipPixels;
        case GL_PACK_SKIP_IMAGES: return PixelStoreParam::PackSkipImages;
        case GL_PACK_SWAP_BYTES: return PixelStoreParam::PackSwapBytes;
        case GL_PACK_LSB_FIRST: return PixelStoreParam::PackLSBFirst;
        case GL_UNPACK_ALIGNMENT: return PixelStoreParam::UnpackAlignment;
        case GL_UNPACK_ROW_LENGTH: return PixelStoreParam::UnpackRowLength;
        case GL_UNPACK_IMAGE_HEIGHT: return PixelStoreParam::UnpackImageHeight;
        case GL_UNPACK_SKIP_ROWS: return PixelStoreParam::UnpackSkipRows;
        case GL_UNPACK_SKIP_PIXELS: return PixelStoreParam::UnpackSkipPixels;
        case GL_UNPACK_SKIP_IMAGES: return PixelStoreParam::UnpackSkipImages;
        case GL_UNPACK_SWAP_BYTES: return PixelStoreParam::UnpackSwapBytes;
        case GL_UNPACK_LSB_FIRST: return PixelStoreParam::UnpackLSBFirst;
        default: return PixelStoreParam::Unknown;
    }
}

GLenum PixelStoreParamToGL(PixelStoreParam v) {
    switch (v) {
        case PixelStoreParam::PackAlignment: return GL_PACK_ALIGNMENT;
        case PixelStoreParam::PackRowLength: return GL_PACK_ROW_LENGTH;
        case PixelStoreParam::PackImageHeight: return GL_PACK_IMAGE_HEIGHT;
        case PixelStoreParam::PackSkipRows: return GL_PACK_SKIP_ROWS;
        case PixelStoreParam::PackSkipPixels: return GL_PACK_SKIP_PIXELS;
        case PixelStoreParam::PackSkipImages: return GL_PACK_SKIP_IMAGES;
        case PixelStoreParam::PackSwapBytes: return GL_PACK_SWAP_BYTES;
        case PixelStoreParam::PackLSBFirst: return GL_PACK_LSB_FIRST;
        case PixelStoreParam::UnpackAlignment: return GL_UNPACK_ALIGNMENT;
        case PixelStoreParam::UnpackRowLength: return GL_UNPACK_ROW_LENGTH;
        case PixelStoreParam::UnpackImageHeight: return GL_UNPACK_IMAGE_HEIGHT;
        case PixelStoreParam::UnpackSkipRows: return GL_UNPACK_SKIP_ROWS;
        case PixelStoreParam::UnpackSkipPixels: return GL_UNPACK_SKIP_PIXELS;
        case PixelStoreParam::UnpackSkipImages: return GL_UNPACK_SKIP_IMAGES;
        case PixelStoreParam::UnpackSwapBytes: return GL_UNPACK_SWAP_BYTES;
        case PixelStoreParam::UnpackLSBFirst: return GL_UNPACK_LSB_FIRST;
        case PixelStoreParam::PixelStoreParamCount: return GL_NONE;
        case PixelStoreParam::Unknown: return GL_NONE;
        default: return GL_NONE;
    }
}

} // namespace mithril::glstate
