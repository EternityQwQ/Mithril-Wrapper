// Mithril-Wrapper - MG_State/GLState/RenderState/RenderState.cpp
//
// Implementation of the RenderState domain component. See RenderState.h for
// the shared API contract (typed-enum setters, single version bump per
// mutating setter only when the value actually changes, no GL<->enum
// translation performed in here).
#include "RenderState.h"

#include <cstring>

namespace mithril::glstate {

namespace {

// Map a StencilFace to its slot index in RenderStateParameters::StencilStates
// (0 = Front, 1 = Back). Anything other than Back collapses to Front so an
// unexpected enum never reads out of bounds.
size_t StencilFaceIndex(StencilFace face) {
    return face == StencilFace::Back ? 1 : 0;
}

} // namespace

RenderState::RenderState() {
    // All defaults come from the default member initializers in
    // RenderStateParameters / PerBufferBlendState / StencilFaceState /
    // BoolVec4 (which is all-true by default, giving the spec-mandated
    // GL_COLOR_WRITEMASK = (true, true, true, true) for every draw buffer).
}

uint16_t RenderState::GetVersion() const {
    return m_version;
}

const RenderStateParameters& RenderState::GetAllParameters() const {
    return m_parameters;
}

// -------------------- Rasterization --------------------
void RenderState::SetViewport(int x, int y, int w, int h) {
    IntRect rect{x, y, w, h};
    if (m_parameters.Viewport == rect) return;
    m_parameters.Viewport = rect;
    ++m_version;
}

IntRect RenderState::GetViewport() const {
    return m_parameters.Viewport;
}

void RenderState::SetLineWidth(float width) {
    if (m_parameters.LineWidth == width) return;
    m_parameters.LineWidth = width;
    ++m_version;
}

float RenderState::GetLineWidth() const {
    return m_parameters.LineWidth;
}

void RenderState::SetPointSize(float size) {
    if (m_parameters.PointSize == size) return;
    m_parameters.PointSize = size;
    ++m_version;
}

float RenderState::GetPointSize() const {
    return m_parameters.PointSize;
}

void RenderState::SetPolygonOffset(float factor, float units) {
    if (m_parameters.PolygonOffsetFactor == factor &&
        m_parameters.PolygonOffsetUnits == units) {
        return;
    }
    m_parameters.PolygonOffsetFactor = factor;
    m_parameters.PolygonOffsetUnits = units;
    ++m_version;
}

float RenderState::GetPolygonOffsetFactor() const {
    return m_parameters.PolygonOffsetFactor;
}

float RenderState::GetPolygonOffsetUnits() const {
    return m_parameters.PolygonOffsetUnits;
}

// -------------------- Hints / point params / color clamp / polygon mode --------------------
void RenderState::SetHint(GLenum target, GLenum mode) {
    GLenum* slot = nullptr;
    switch (target) {
        case GL_LINE_SMOOTH_HINT: slot = &m_parameters.LineSmoothHint; break;
        case GL_POLYGON_SMOOTH_HINT: slot = &m_parameters.PolygonSmoothHint; break;
        case GL_TEXTURE_COMPRESSION_HINT: slot = &m_parameters.TextureCompressionHint; break;
        case GL_FRAGMENT_SHADER_DERIVATIVE_HINT: slot = &m_parameters.FragmentShaderDerivativeHint; break;
        default: return;
    }
    if (*slot == mode) return;
    *slot = mode;
    ++m_version;
}

GLenum RenderState::GetHint(GLenum target) const {
    switch (target) {
        case GL_LINE_SMOOTH_HINT: return m_parameters.LineSmoothHint;
        case GL_POLYGON_SMOOTH_HINT: return m_parameters.PolygonSmoothHint;
        case GL_TEXTURE_COMPRESSION_HINT: return m_parameters.TextureCompressionHint;
        case GL_FRAGMENT_SHADER_DERIVATIVE_HINT: return m_parameters.FragmentShaderDerivativeHint;
        default: return GL_DONT_CARE;
    }
}

void RenderState::SetPointFadeThresholdSize(float size) {
    if (m_parameters.PointFadeThresholdSize == size) return;
    m_parameters.PointFadeThresholdSize = size;
    ++m_version;
}

float RenderState::GetPointFadeThresholdSize() const {
    return m_parameters.PointFadeThresholdSize;
}

void RenderState::SetPointSpriteCoordOrigin(GLenum origin) {
    if (m_parameters.PointSpriteCoordOrigin == origin) return;
    m_parameters.PointSpriteCoordOrigin = origin;
    ++m_version;
}

GLenum RenderState::GetPointSpriteCoordOrigin() const {
    return m_parameters.PointSpriteCoordOrigin;
}

void RenderState::SetClampReadColor(GLenum clamp) {
    if (m_parameters.ClampReadColor == clamp) return;
    m_parameters.ClampReadColor = clamp;
    ++m_version;
}

GLenum RenderState::GetClampReadColor() const {
    return m_parameters.ClampReadColor;
}

void RenderState::SetPolygonMode(GLenum front, GLenum back) {
    if (m_parameters.PolygonModeFront == front && m_parameters.PolygonModeBack == back) return;
    m_parameters.PolygonModeFront = front;
    m_parameters.PolygonModeBack = back;
    ++m_version;
}

GLenum RenderState::GetPolygonModeFront() const {
    return m_parameters.PolygonModeFront;
}

GLenum RenderState::GetPolygonModeBack() const {
    return m_parameters.PolygonModeBack;
}

void RenderState::SetPrimitiveRestartIndex(uint32_t index) {
    if (m_parameters.PrimitiveRestartIndex == index) return;
    m_parameters.PrimitiveRestartIndex = index;
    ++m_version;
}

uint32_t RenderState::GetPrimitiveRestartIndex() const {
    return m_parameters.PrimitiveRestartIndex;
}

// -------------------- Capabilities --------------------
void RenderState::SetCapability(CapabilityInput cap, bool enabled) {
    // Helper: assign `enabled` to `field` and bump the version on change.
    auto apply = [&](bool& field) {
        if (field != enabled) {
            field = enabled;
            ++m_version;
        }
    };

    switch (cap) {
        case CapabilityInput::Blend: apply(m_parameters.BlendEnabled); break;
        case CapabilityInput::ClipDistance0: apply(m_parameters.ClipDistanceEnabled[0]); break;
        case CapabilityInput::ClipDistance1: apply(m_parameters.ClipDistanceEnabled[1]); break;
        case CapabilityInput::ClipDistance2: apply(m_parameters.ClipDistanceEnabled[2]); break;
        case CapabilityInput::ClipDistance3: apply(m_parameters.ClipDistanceEnabled[3]); break;
        case CapabilityInput::ClipDistance4: apply(m_parameters.ClipDistanceEnabled[4]); break;
        case CapabilityInput::ClipDistance5: apply(m_parameters.ClipDistanceEnabled[5]); break;
        case CapabilityInput::ClipDistance6: apply(m_parameters.ClipDistanceEnabled[6]); break;
        case CapabilityInput::ClipDistance7: apply(m_parameters.ClipDistanceEnabled[7]); break;
        case CapabilityInput::ColorLogicOp: apply(m_parameters.ColorLogicOpEnabled); break;
        case CapabilityInput::CullFace: apply(m_parameters.CullFaceEnabled); break;
        case CapabilityInput::DebugOutput: apply(m_parameters.DebugOutputEnabled); break;
        case CapabilityInput::DebugOutputSynchronous: apply(m_parameters.DebugOutputSynchronousEnabled); break;
        case CapabilityInput::DepthClamp: apply(m_parameters.DepthClampEnabled); break;
        case CapabilityInput::DepthTest: apply(m_parameters.DepthTestEnabled); break;
        case CapabilityInput::Dither: apply(m_parameters.DitherEnabled); break;
        case CapabilityInput::FramebufferSrgb: apply(m_parameters.FramebufferSrgbEnabled); break;
        case CapabilityInput::LineSmooth: apply(m_parameters.LineSmoothEnabled); break;
        case CapabilityInput::Multisample: apply(m_parameters.MultisampleEnabled); break;
        case CapabilityInput::PolygonOffsetFill: apply(m_parameters.PolygonOffsetFillEnabled); break;
        case CapabilityInput::PolygonOffsetLine: apply(m_parameters.PolygonOffsetLineEnabled); break;
        case CapabilityInput::PolygonOffsetPoint: apply(m_parameters.PolygonOffsetPointEnabled); break;
        case CapabilityInput::PolygonSmooth: apply(m_parameters.PolygonSmoothEnabled); break;
        case CapabilityInput::PrimitiveRestart: apply(m_parameters.PrimitiveRestartEnabled); break;
        case CapabilityInput::PrimitiveRestartFixedIndex: apply(m_parameters.PrimitiveRestartFixedIndexEnabled); break;
        case CapabilityInput::RasterizerDiscard: apply(m_parameters.RasterizerDiscardEnabled); break;
        case CapabilityInput::SampleAlphaToCoverage: apply(m_parameters.SampleAlphaToCoverageEnabled); break;
        case CapabilityInput::SampleAlphaToOne: apply(m_parameters.SampleAlphaToOneEnabled); break;
        case CapabilityInput::SampleCoverage: apply(m_parameters.SampleCoverageEnabled); break;
        case CapabilityInput::SampleShading: apply(m_parameters.SampleShadingEnabled); break;
        case CapabilityInput::SampleMask: apply(m_parameters.SampleMaskEnabled); break;
        case CapabilityInput::ScissorTest: apply(m_parameters.ScissorTestEnabled); break;
        case CapabilityInput::StencilTest: apply(m_parameters.StencilTestEnabled); break;
        case CapabilityInput::TextureCubeMapSeamless: apply(m_parameters.TextureCubeMapSeamlessEnabled); break;
        case CapabilityInput::ProgramPointSize: apply(m_parameters.ProgramPointSizeEnabled); break;
        default: break;
    }
}

bool RenderState::IsCapabilityEnabled(CapabilityInput cap) const {
    switch (cap) {
        case CapabilityInput::Blend: return m_parameters.BlendEnabled;
        case CapabilityInput::ClipDistance0: return m_parameters.ClipDistanceEnabled[0];
        case CapabilityInput::ClipDistance1: return m_parameters.ClipDistanceEnabled[1];
        case CapabilityInput::ClipDistance2: return m_parameters.ClipDistanceEnabled[2];
        case CapabilityInput::ClipDistance3: return m_parameters.ClipDistanceEnabled[3];
        case CapabilityInput::ClipDistance4: return m_parameters.ClipDistanceEnabled[4];
        case CapabilityInput::ClipDistance5: return m_parameters.ClipDistanceEnabled[5];
        case CapabilityInput::ClipDistance6: return m_parameters.ClipDistanceEnabled[6];
        case CapabilityInput::ClipDistance7: return m_parameters.ClipDistanceEnabled[7];
        case CapabilityInput::ColorLogicOp: return m_parameters.ColorLogicOpEnabled;
        case CapabilityInput::CullFace: return m_parameters.CullFaceEnabled;
        case CapabilityInput::DebugOutput: return m_parameters.DebugOutputEnabled;
        case CapabilityInput::DebugOutputSynchronous: return m_parameters.DebugOutputSynchronousEnabled;
        case CapabilityInput::DepthClamp: return m_parameters.DepthClampEnabled;
        case CapabilityInput::DepthTest: return m_parameters.DepthTestEnabled;
        case CapabilityInput::Dither: return m_parameters.DitherEnabled;
        case CapabilityInput::FramebufferSrgb: return m_parameters.FramebufferSrgbEnabled;
        case CapabilityInput::LineSmooth: return m_parameters.LineSmoothEnabled;
        case CapabilityInput::Multisample: return m_parameters.MultisampleEnabled;
        case CapabilityInput::PolygonOffsetFill: return m_parameters.PolygonOffsetFillEnabled;
        case CapabilityInput::PolygonOffsetLine: return m_parameters.PolygonOffsetLineEnabled;
        case CapabilityInput::PolygonOffsetPoint: return m_parameters.PolygonOffsetPointEnabled;
        case CapabilityInput::PolygonSmooth: return m_parameters.PolygonSmoothEnabled;
        case CapabilityInput::PrimitiveRestart: return m_parameters.PrimitiveRestartEnabled;
        case CapabilityInput::PrimitiveRestartFixedIndex: return m_parameters.PrimitiveRestartFixedIndexEnabled;
        case CapabilityInput::RasterizerDiscard: return m_parameters.RasterizerDiscardEnabled;
        case CapabilityInput::SampleAlphaToCoverage: return m_parameters.SampleAlphaToCoverageEnabled;
        case CapabilityInput::SampleAlphaToOne: return m_parameters.SampleAlphaToOneEnabled;
        case CapabilityInput::SampleCoverage: return m_parameters.SampleCoverageEnabled;
        case CapabilityInput::SampleShading: return m_parameters.SampleShadingEnabled;
        case CapabilityInput::SampleMask: return m_parameters.SampleMaskEnabled;
        case CapabilityInput::ScissorTest: return m_parameters.ScissorTestEnabled;
        case CapabilityInput::StencilTest: return m_parameters.StencilTestEnabled;
        case CapabilityInput::TextureCubeMapSeamless: return m_parameters.TextureCubeMapSeamlessEnabled;
        case CapabilityInput::ProgramPointSize: return m_parameters.ProgramPointSizeEnabled;
        default: return false;
    }
}

// -------------------- Blending --------------------
void RenderState::SetBlendFunc(BlendFactor srcRGB, BlendFactor dstRGB,
                               BlendFactor srcAlpha, BlendFactor dstAlpha) {
    bool changed = false;
    for (auto& state : m_parameters.BlendStates) {
        if (state.SrcFactorRGB == srcRGB && state.DstFactorRGB == dstRGB &&
            state.SrcFactorAlpha == srcAlpha && state.DstFactorAlpha == dstAlpha) {
            continue;
        }
        state.SrcFactorRGB = srcRGB;
        state.DstFactorRGB = dstRGB;
        state.SrcFactorAlpha = srcAlpha;
        state.DstFactorAlpha = dstAlpha;
        changed = true;
    }
    if (changed) ++m_version;
}

void RenderState::GetBlendFunc(BlendFactor& srcRGB, BlendFactor& dstRGB,
                               BlendFactor& srcAlpha, BlendFactor& dstAlpha) const {
    const auto& state = m_parameters.BlendStates[0];
    srcRGB = state.SrcFactorRGB;
    dstRGB = state.DstFactorRGB;
    srcAlpha = state.SrcFactorAlpha;
    dstAlpha = state.DstFactorAlpha;
}

void RenderState::SetBlendFuncIndexed(uint32_t index, BlendFactor srcRGB, BlendFactor dstRGB,
                                      BlendFactor srcAlpha, BlendFactor dstAlpha) {
    if (index >= kMaxDrawBuffers) return;
    PerBufferBlendState& state = m_parameters.BlendStates[index];
    if (state.SrcFactorRGB == srcRGB && state.DstFactorRGB == dstRGB &&
        state.SrcFactorAlpha == srcAlpha && state.DstFactorAlpha == dstAlpha) {
        return;
    }
    state.SrcFactorRGB = srcRGB;
    state.DstFactorRGB = dstRGB;
    state.SrcFactorAlpha = srcAlpha;
    state.DstFactorAlpha = dstAlpha;
    ++m_version;
}

void RenderState::GetBlendFuncIndexed(uint32_t index, BlendFactor& srcRGB, BlendFactor& dstRGB,
                                      BlendFactor& srcAlpha, BlendFactor& dstAlpha) const {
    if (index >= kMaxDrawBuffers) {
        srcRGB = dstRGB = srcAlpha = dstAlpha = BlendFactor::Zero;
        return;
    }
    const auto& state = m_parameters.BlendStates[index];
    srcRGB = state.SrcFactorRGB;
    dstRGB = state.DstFactorRGB;
    srcAlpha = state.SrcFactorAlpha;
    dstAlpha = state.DstFactorAlpha;
}

void RenderState::SetBlendEquation(BlendEquation color, BlendEquation alpha) {
    bool changed = false;
    for (auto& state : m_parameters.BlendStates) {
        if (state.ColorEquation == color && state.AlphaEquation == alpha) continue;
        state.ColorEquation = color;
        state.AlphaEquation = alpha;
        changed = true;
    }
    if (changed) ++m_version;
}

void RenderState::GetBlendEquation(BlendEquation& color, BlendEquation& alpha) const {
    const auto& state = m_parameters.BlendStates[0];
    color = state.ColorEquation;
    alpha = state.AlphaEquation;
}

void RenderState::SetBlendEquationIndexed(uint32_t index, BlendEquation color, BlendEquation alpha) {
    if (index >= kMaxDrawBuffers) return;
    PerBufferBlendState& state = m_parameters.BlendStates[index];
    if (state.ColorEquation == color && state.AlphaEquation == alpha) return;
    state.ColorEquation = color;
    state.AlphaEquation = alpha;
    ++m_version;
}

void RenderState::GetBlendEquationIndexed(uint32_t index, BlendEquation& color, BlendEquation& alpha) const {
    if (index >= kMaxDrawBuffers) {
        color = alpha = BlendEquation::Add;
        return;
    }
    const auto& state = m_parameters.BlendStates[index];
    color = state.ColorEquation;
    alpha = state.AlphaEquation;
}

void RenderState::SetBlendColor(const float color[4]) {
    if (std::memcmp(m_parameters.BlendColor, color, sizeof(m_parameters.BlendColor)) == 0) return;
    std::memcpy(m_parameters.BlendColor, color, sizeof(m_parameters.BlendColor));
    ++m_version;
}

const float* RenderState::GetBlendColor() const {
    return m_parameters.BlendColor;
}

void RenderState::SetLogicOp(LogicOperation op) {
    if (m_parameters.LogicOp == op) return;
    m_parameters.LogicOp = op;
    ++m_version;
}

LogicOperation RenderState::GetLogicOp() const {
    return m_parameters.LogicOp;
}

// -------------------- Depth --------------------
void RenderState::SetDepthFunc(DepthTestFunc func) {
    if (m_parameters.DepthFunc == func) return;
    m_parameters.DepthFunc = func;
    ++m_version;
}

DepthTestFunc RenderState::GetDepthFunc() const {
    return m_parameters.DepthFunc;
}

void RenderState::SetDepthMask(bool flag) {
    if (m_parameters.DepthMask == flag) return;
    m_parameters.DepthMask = flag;
    ++m_version;
}

bool RenderState::GetDepthMask() const {
    return m_parameters.DepthMask;
}

void RenderState::SetDepthRange(float n, float f) {
    if (m_parameters.DepthRange[0] == n && m_parameters.DepthRange[1] == f) return;
    m_parameters.DepthRange[0] = n;
    m_parameters.DepthRange[1] = f;
    ++m_version;
}

void RenderState::GetDepthRange(float& n, float& f) const {
    n = m_parameters.DepthRange[0];
    f = m_parameters.DepthRange[1];
}

// -------------------- Stencil --------------------
void RenderState::SetStencilFunc(StencilFace face, DepthTestFunc func, int ref, uint32_t mask) {
    StencilFaceState& state = m_parameters.StencilStates[StencilFaceIndex(face)];
    if (state.Func == func && state.Ref == ref && state.ValueMask == mask) return;
    state.Func = func;
    state.Ref = ref;
    state.ValueMask = mask;
    ++m_version;
}

void RenderState::SetStencilMask(StencilFace face, uint32_t mask) {
    StencilFaceState& state = m_parameters.StencilStates[StencilFaceIndex(face)];
    if (state.WriteMask == mask) return;
    state.WriteMask = mask;
    ++m_version;
}

void RenderState::SetStencilOp(StencilFace face, StencilOperation fail,
                               StencilOperation dpfail, StencilOperation dppass) {
    StencilFaceState& state = m_parameters.StencilStates[StencilFaceIndex(face)];
    if (state.FailOp == fail && state.PassDepthFailOp == dpfail &&
        state.PassDepthPassOp == dppass) {
        return;
    }
    state.FailOp = fail;
    state.PassDepthFailOp = dpfail;
    state.PassDepthPassOp = dppass;
    ++m_version;
}

const StencilFaceState& RenderState::GetStencilState(StencilFace face) const {
    return m_parameters.StencilStates[StencilFaceIndex(face)];
}

// -------------------- Color mask --------------------
void RenderState::SetColorMask(BoolVec4 mask) {
    // glColorMask broadcasts the same mask to every draw buffer.
    bool changed = false;
    for (auto& slot : m_parameters.ColorMasks) {
        if (!(slot == mask)) {
            slot = mask;
            changed = true;
        }
    }
    if (changed) ++m_version;
}

BoolVec4 RenderState::GetColorMask() const {
    // Non-indexed query reports draw buffer 0.
    return m_parameters.ColorMasks[0];
}

void RenderState::SetColorMaskIndexed(uint32_t index, BoolVec4 mask) {
    if (index >= kMaxDrawBuffers) return;
    if (m_parameters.ColorMasks[index] == mask) return;
    m_parameters.ColorMasks[index] = mask;
    ++m_version;
}

BoolVec4 RenderState::GetColorMaskIndexed(uint32_t index) const {
    if (index >= kMaxDrawBuffers) return BoolVec4{};
    return m_parameters.ColorMasks[index];
}

// -------------------- Clear values --------------------
void RenderState::SetClearColor(const float color[4]) {
    if (std::memcmp(m_parameters.ClearColor, color, sizeof(m_parameters.ClearColor)) == 0) return;
    std::memcpy(m_parameters.ClearColor, color, sizeof(m_parameters.ClearColor));
    ++m_version;
}

const float* RenderState::GetClearColor() const {
    return m_parameters.ClearColor;
}

void RenderState::SetClearDepth(float depth) {
    if (m_parameters.ClearDepth == depth) return;
    m_parameters.ClearDepth = depth;
    ++m_version;
}

float RenderState::GetClearDepth() const {
    return m_parameters.ClearDepth;
}

void RenderState::SetClearStencil(uint32_t stencil) {
    if (m_parameters.ClearStencil == stencil) return;
    m_parameters.ClearStencil = stencil;
    ++m_version;
}

uint32_t RenderState::GetClearStencil() const {
    return m_parameters.ClearStencil;
}

// -------------------- Multisample coverage --------------------
void RenderState::SetSampleCoverage(float value, bool invert) {
    if (m_parameters.SampleCoverageValue == value &&
        m_parameters.SampleCoverageInvert == invert) {
        return;
    }
    m_parameters.SampleCoverageValue = value;
    m_parameters.SampleCoverageInvert = invert;
    ++m_version;
}

float RenderState::GetSampleCoverageValue() const {
    return m_parameters.SampleCoverageValue;
}

bool RenderState::GetSampleCoverageInvert() const {
    return m_parameters.SampleCoverageInvert;
}

void RenderState::SetSampleMaskValue(uint32_t mask) {
    if (m_parameters.SampleMaskValue == mask) return;
    m_parameters.SampleMaskValue = mask;
    ++m_version;
}

uint32_t RenderState::GetSampleMaskValue() const {
    return m_parameters.SampleMaskValue;
}

// -------------------- Pixel store --------------------
void RenderState::SetPixelStoreParam(PixelStoreParam param, int value) {
    switch (param) {
        case PixelStoreParam::PackAlignment:
            if (m_pixelStorePack.Alignment != value) { m_pixelStorePack.Alignment = value; ++m_version; }
            break;
        case PixelStoreParam::PackRowLength:
            if (m_pixelStorePack.RowLength != value) { m_pixelStorePack.RowLength = value; ++m_version; }
            break;
        case PixelStoreParam::PackImageHeight:
            if (m_pixelStorePack.ImageHeight != value) { m_pixelStorePack.ImageHeight = value; ++m_version; }
            break;
        case PixelStoreParam::PackSkipRows:
            if (m_pixelStorePack.SkipRows != value) { m_pixelStorePack.SkipRows = value; ++m_version; }
            break;
        case PixelStoreParam::PackSkipPixels:
            if (m_pixelStorePack.SkipPixels != value) { m_pixelStorePack.SkipPixels = value; ++m_version; }
            break;
        case PixelStoreParam::PackSkipImages:
            if (m_pixelStorePack.SkipImages != value) { m_pixelStorePack.SkipImages = value; ++m_version; }
            break;
        case PixelStoreParam::PackSwapBytes: {
            bool b = value != 0;
            if (m_pixelStorePack.SwapBytes != b) { m_pixelStorePack.SwapBytes = b; ++m_version; }
            break;
        }
        case PixelStoreParam::PackLSBFirst: {
            bool b = value != 0;
            if (m_pixelStorePack.LSBFirst != b) { m_pixelStorePack.LSBFirst = b; ++m_version; }
            break;
        }
        case PixelStoreParam::UnpackAlignment:
            if (m_pixelStoreUnpack.Alignment != value) { m_pixelStoreUnpack.Alignment = value; ++m_version; }
            break;
        case PixelStoreParam::UnpackRowLength:
            if (m_pixelStoreUnpack.RowLength != value) { m_pixelStoreUnpack.RowLength = value; ++m_version; }
            break;
        case PixelStoreParam::UnpackImageHeight:
            if (m_pixelStoreUnpack.ImageHeight != value) { m_pixelStoreUnpack.ImageHeight = value; ++m_version; }
            break;
        case PixelStoreParam::UnpackSkipRows:
            if (m_pixelStoreUnpack.SkipRows != value) { m_pixelStoreUnpack.SkipRows = value; ++m_version; }
            break;
        case PixelStoreParam::UnpackSkipPixels:
            if (m_pixelStoreUnpack.SkipPixels != value) { m_pixelStoreUnpack.SkipPixels = value; ++m_version; }
            break;
        case PixelStoreParam::UnpackSkipImages:
            if (m_pixelStoreUnpack.SkipImages != value) { m_pixelStoreUnpack.SkipImages = value; ++m_version; }
            break;
        case PixelStoreParam::UnpackSwapBytes: {
            bool b = value != 0;
            if (m_pixelStoreUnpack.SwapBytes != b) { m_pixelStoreUnpack.SwapBytes = b; ++m_version; }
            break;
        }
        case PixelStoreParam::UnpackLSBFirst: {
            bool b = value != 0;
            if (m_pixelStoreUnpack.LSBFirst != b) { m_pixelStoreUnpack.LSBFirst = b; ++m_version; }
            break;
        }
        default: break;
    }
}

int RenderState::GetPixelStoreParam(PixelStoreParam param) const {
    switch (param) {
        case PixelStoreParam::PackAlignment: return m_pixelStorePack.Alignment;
        case PixelStoreParam::PackRowLength: return m_pixelStorePack.RowLength;
        case PixelStoreParam::PackImageHeight: return m_pixelStorePack.ImageHeight;
        case PixelStoreParam::PackSkipRows: return m_pixelStorePack.SkipRows;
        case PixelStoreParam::PackSkipPixels: return m_pixelStorePack.SkipPixels;
        case PixelStoreParam::PackSkipImages: return m_pixelStorePack.SkipImages;
        case PixelStoreParam::PackSwapBytes: return m_pixelStorePack.SwapBytes ? 1 : 0;
        case PixelStoreParam::PackLSBFirst: return m_pixelStorePack.LSBFirst ? 1 : 0;
        case PixelStoreParam::UnpackAlignment: return m_pixelStoreUnpack.Alignment;
        case PixelStoreParam::UnpackRowLength: return m_pixelStoreUnpack.RowLength;
        case PixelStoreParam::UnpackImageHeight: return m_pixelStoreUnpack.ImageHeight;
        case PixelStoreParam::UnpackSkipRows: return m_pixelStoreUnpack.SkipRows;
        case PixelStoreParam::UnpackSkipPixels: return m_pixelStoreUnpack.SkipPixels;
        case PixelStoreParam::UnpackSkipImages: return m_pixelStoreUnpack.SkipImages;
        case PixelStoreParam::UnpackSwapBytes: return m_pixelStoreUnpack.SwapBytes ? 1 : 0;
        case PixelStoreParam::UnpackLSBFirst: return m_pixelStoreUnpack.LSBFirst ? 1 : 0;
        default: return 0;
    }
}

PixelStoreParameters RenderState::GetPixelStoreParameters(bool isUnpack) const {
    return isUnpack ? m_pixelStoreUnpack : m_pixelStorePack;
}

// -------------------- Cull face / front face --------------------
void RenderState::SetCullFaceMode(CullFaceMode mode) {
    if (m_parameters.CullFaceModeSetting == mode) return;
    m_parameters.CullFaceModeSetting = mode;
    ++m_version;
}

CullFaceMode RenderState::GetCullFaceMode() const {
    return m_parameters.CullFaceModeSetting;
}

void RenderState::SetFrontFaceMode(FrontFaceMode mode) {
    if (m_parameters.FrontFaceModeSetting == mode) return;
    m_parameters.FrontFaceModeSetting = mode;
    ++m_version;
}

FrontFaceMode RenderState::GetFrontFaceMode() const {
    return m_parameters.FrontFaceModeSetting;
}

// -------------------- Scissor --------------------
void RenderState::SetScissorBox(int x, int y, int w, int h) {
    IntRect rect{x, y, w, h};
    if (m_parameters.ScissorBox == rect) return;
    m_parameters.ScissorBox = rect;
    ++m_version;
}

IntRect RenderState::GetScissorBox() const {
    return m_parameters.ScissorBox;
}

} // namespace mithril::glstate
