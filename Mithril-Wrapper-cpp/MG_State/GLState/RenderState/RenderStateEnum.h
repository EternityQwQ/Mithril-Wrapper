// Mithril-Wrapper - MG_State/GLState/RenderState/RenderStateEnum.h
//
// Strongly-typed enum classes for the OpenGL render-state surface. These
// replace the bare GLenum fields that were scattered through the flat
// MG_State/State.h GLState, giving the new modular state machine type-safe
// value domains for every render-state knob.
//
// Each enum ends with a `*Count` sentinel (usable as an array bound / iteration
// limit) and `Unknown = -1` (the result of translating an unrecognised GLenum,
// left for the MG_Impl layer to report as GL_INVALID_ENUM). The enumerator
// ordering follows the standard OpenGL 3.3 Core value domains.
//
// This header is intentionally GL-agnostic (no <GL/gl.h> dependency): it only
// defines value domains. The GL <-> enum translation lives in
// RenderStateEnumConverter.h/.cpp.
#pragma once

namespace mithril::glstate {

// glBlendFunc / glBlendFunci source and destination factors.
enum class BlendFactor {
    Zero,
    One,
    SrcColor,
    OneMinusSrcColor,
    DstColor,
    OneMinusDstColor,
    SrcAlpha,
    OneMinusSrcAlpha,
    DstAlpha,
    OneMinusDstAlpha,
    ConstantColor,
    OneMinusConstantColor,
    ConstantAlpha,
    OneMinusConstantAlpha,
    // Dual-source blend factors (GL_SRC1_*, glBindFragDataLocationIndexed).
    Src1Color,
    OneMinusSrc1Color,
    Src1Alpha,
    OneMinusSrc1Alpha,
    BlendFactorCount,
    Unknown = -1
};

// glBlendEquation / glBlendEquationi RGB and alpha equations.
enum class BlendEquation {
    Add,
    Subtract,
    ReverseSubtract,
    Min,
    Max,
    BlendEquationCount,
    Unknown = -1
};

// glLogicOp color logical operation (enabled via GL_COLOR_LOGIC_OP).
enum class LogicOperation {
    Clear,
    And,
    AndReverse,
    Copy,
    AndInverted,
    Noop,
    Xor,
    Or,
    Nor,
    Equiv,
    Invert,
    OrReverse,
    CopyInverted,
    OrInverted,
    Nand,
    Set,
    LogicOperationCount,
    Unknown = -1
};

// glDepthFunc depth-comparison function.
enum class DepthTestFunc {
    Never,
    Less,
    Equal,
    LessEqual,
    Greater,
    NotEqual,
    GreaterEqual,
    Always,
    DepthTestFuncCount,
    Unknown = -1
};

// glStencilOp stencil test fail / depth fail / depth pass operations.
enum class StencilOperation {
    Keep,
    Zero,
    Replace,
    IncrementClamp,
    DecrementClamp,
    Invert,
    IncrementWrap,
    DecrementWrap,
    StencilOperationCount,
    Unknown = -1
};

// Single stencil face. GL_FRONT_AND_BACK is split into front+back by the caller
// before being routed through this enum, so only the two single-face targets
// are representable here.
enum class StencilFace {
    Front,
    Back,
    StencilFaceCount,
    Unknown = -1
};

// glCullFace polygon culling mode.
enum class CullFaceMode {
    Front,
    Back,
    FrontAndBack,
    CullFaceModeCount,
    Unknown = -1
};

// glFrontFace front-facing winding order.
enum class FrontFaceMode {
    CounterClockwise,
    Clockwise,
    FrontFaceModeCount,
    Unknown = -1
};

// glPixelStorei pack / unpack pixel-storage parameters.
enum class PixelStoreParam {
    // Pack parameters (glReadPixels and friends).
    PackAlignment,
    PackRowLength,
    PackImageHeight,
    PackSkipRows,
    PackSkipPixels,
    PackSkipImages,
    PackSwapBytes,
    PackLSBFirst,

    // Unpack parameters (glTexImage* / glTexSubImage* and friends).
    UnpackAlignment,
    UnpackRowLength,
    UnpackImageHeight,
    UnpackSkipRows,
    UnpackSkipPixels,
    UnpackSkipImages,
    UnpackSwapBytes,
    UnpackLSBFirst,

    PixelStoreParamCount,
    Unknown = -1
};

// glEnable / glDisable capability targets (GL 3.3 Core surface).
enum class CapabilityInput {
    Blend,
    ClipDistance0,
    ClipDistance1,
    ClipDistance2,
    ClipDistance3,
    ClipDistance4,
    ClipDistance5,
    ClipDistance6,
    ClipDistance7,
    ColorLogicOp,
    CullFace,
    DebugOutput,
    DebugOutputSynchronous,
    DepthClamp,
    DepthTest,
    Dither,
    FramebufferSrgb,
    LineSmooth,
    Multisample,
    PolygonOffsetFill,
    PolygonOffsetLine,
    PolygonOffsetPoint,
    PolygonSmooth,
    PrimitiveRestart,
    PrimitiveRestartFixedIndex,
    RasterizerDiscard,
    SampleAlphaToCoverage,
    SampleAlphaToOne,
    SampleCoverage,
    SampleShading,
    SampleMask,
    ScissorTest,
    StencilTest,
    TextureCubeMapSeamless,
    ProgramPointSize,
    CapabilityInputCount,
    Unknown = -1
};

} // namespace mithril::glstate
