// Mithril-Wrapper - MG_State/GLState/RenderState/RenderState.h
//
// RenderState: the largest domain component of the modular OpenGL state
// machine. It owns the full GL 3.3 Core render-state surface (viewport,
// rasterization, blend, depth, stencil, color mask, clear values,
// multisample coverage, pixel store and the capability enables) together
// with a version counter that is bumped whenever a setter actually changes
// a value, so a backend can cheaply tell whether the cached pipeline needs
// rebuilding.
//
// Shared API contract:
//   * Setters receive the strongly-typed enum classes from RenderStateEnum.h.
//     The GL <-> enum translation is the MG_Impl entry layer's responsibility,
//     so RenderState itself never calls the RenderStateEnumConverter helpers.
//   * Every setter that mutates a value bumps `m_version` exactly once, and
//     only when the new value differs from the old (`if (old != new)
//     { m_x = new; ++m_version; }`). Broadcast setters (glBlendFunc /
//     glBlendEquation / glColorMask) bump once if any slot changed.
//   * A small number of GL passthrough knobs (glPolygonMode, glHint,
//     glPointParameterf(GL_POINT_SPRITE_COORD_ORIGIN), glClampColor) are kept
//     as raw GLenum because this codebase has no typed enum for them; the
//     MG_Impl layer validates them before forwarding.
//
// The render-state fields migrated here replace the bare-GLenum fields that
// were scattered through the flat MG_State/State.h GLState (clearColor,
// depthFunc, blendSrcRGB, stencilFunc, cullMode, viewportX/Y/W/H, ...).
#pragma once

#include <array>
#include <cstdint>

#include <GL/gl.h>

#include "../Common.h"
#include "RenderStateEnum.h"

// A few GL 3.3 Core enum constants used as default values below are missing
// from this repository's minimal glcorearb.h. Define them with the standard
// Khronos registry values under #ifndef guards, mirroring the same workaround
// already used by include/GL/gl.h for GL_PROXY_TEXTURE_2D and friends.
#ifndef GL_UPPER_LEFT
#define GL_UPPER_LEFT 0x812C
#endif
#ifndef GL_LOWER_LEFT
#define GL_LOWER_LEFT 0x812E
#endif
#ifndef GL_CLAMP_READ_COLOR
#define GL_CLAMP_READ_COLOR 0x891C
#endif
#ifndef GL_FIXED_ONLY
#define GL_FIXED_ONLY 0x891D
#endif
#ifndef GL_FRAGMENT_SHADER_DERIVATIVE_HINT
#define GL_FRAGMENT_SHADER_DERIVATIVE_HINT 0x8B8B
#endif

namespace mithril::glstate {

// Per-draw-buffer blend state (glBlendFunci / glBlendEquationi). The
// non-indexed glBlendFunc / glBlendEquation entry points broadcast the same
// factors/equation to every slot. The per-buffer `Enabled` flag is the indexed
// blend enable (glEnablei(GL_BLEND, i)); the global GL_BLEND main switch lives
// as `RenderStateParameters::BlendEnabled` and is toggled via SetCapability.
struct PerBufferBlendState {
    bool Enabled = false;
    BlendFactor SrcFactorRGB = BlendFactor::One;
    BlendFactor DstFactorRGB = BlendFactor::Zero;
    BlendFactor SrcFactorAlpha = BlendFactor::One;
    BlendFactor DstFactorAlpha = BlendFactor::Zero;
    BlendEquation ColorEquation = BlendEquation::Add;
    BlendEquation AlphaEquation = BlendEquation::Add;
};

// State for a single stencil face (GL_FRONT or GL_BACK). GL_FRONT_AND_BACK is
// split into front + back by the MG_Impl caller before reaching RenderState,
// so only the two single-face targets are representable here.
struct StencilFaceState {
    DepthTestFunc Func = DepthTestFunc::Always;
    int Ref = 0;
    uint32_t ValueMask = 0xffffffffu;
    uint32_t WriteMask = 0xffffffffu;
    StencilOperation FailOp = StencilOperation::Keep;
    StencilOperation PassDepthFailOp = StencilOperation::Keep;
    StencilOperation PassDepthPassOp = StencilOperation::Keep;
};

// Pack / unpack pixel-storage parameters (glPixelStorei). Stored twice on
// RenderState: once for pack (glReadPixels) and once for unpack (glTexImage*).
struct PixelStoreParameters {
    bool SwapBytes = false;
    bool LSBFirst = false;
    int RowLength = 0;
    int ImageHeight = 0;
    int SkipPixels = 0;
    int SkipRows = 0;
    int SkipImages = 0;
    int Alignment = 4;
};

// Axis-aligned integer rectangle used for the viewport and scissor box:
// x, y, w, h.
struct IntRect {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;

    bool operator==(const IntRect&) const = default;
};

// Aggregate of every render-state field. Default member initializers match
// the OpenGL 3.3 Core spec defaults (ClearColor 0,0,0,0; ClearDepth 1.0;
// DepthFunc Less; DepthMask true; blend SrcRGB One / DstRGB Zero; color masks
// all true; CullFaceMode Back; FrontFace CCW; Dither true; Multisample true;
// Viewport 0,0,0,0; ...). The per-draw-buffer arrays rely on the default
// constructors of PerBufferBlendState / BoolVec4 / StencilFaceState.
struct RenderStateParameters {
    // ---- Rasterization ----
    IntRect Viewport;                       // x, y, w, h
    float LineWidth = 1.0f;
    float PointSize = 1.0f;
    float PolygonOffsetFactor = 0.0f;
    float PolygonOffsetUnits = 0.0f;

    // ---- Blending (per draw buffer) ----
    std::array<PerBufferBlendState, kMaxDrawBuffers> BlendStates{};
    bool BlendEnabled = false;              // GL_BLEND main switch (glEnable(GL_BLEND))
    LogicOperation LogicOp = LogicOperation::Copy;

    // ---- Depth ----
    bool DepthTestEnabled = false;
    DepthTestFunc DepthFunc = DepthTestFunc::Less;
    bool DepthMask = true;

    // ---- Color writemask (per draw buffer). BoolVec4 defaults to all-true. ----
    std::array<BoolVec4, kMaxDrawBuffers> ColorMasks{};

    // ---- Clear values & blend constant ----
    float ClearColor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float ClearDepth = 1.0f;
    uint32_t ClearStencil = 0;
    float BlendColor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float DepthRange[2] = {0.0f, 1.0f};

    // ---- Multisample coverage ----
    float SampleCoverageValue = 1.0f;
    bool SampleCoverageInvert = false;
    uint32_t SampleMaskValue = 0xffffffffu;

    // ---- Stencil (index 0 = Front, 1 = Back) ----
    std::array<StencilFaceState, 2> StencilStates{};

    // ---- Cull face ----
    bool CullFaceEnabled = false;
    CullFaceMode CullFaceModeSetting = CullFaceMode::Back;
    FrontFaceMode FrontFaceModeSetting = FrontFaceMode::CounterClockwise;

    // ---- Hints (glHint). GL 3.3 core targets default to GL_DONT_CARE. ----
    GLenum LineSmoothHint = GL_DONT_CARE;
    GLenum PolygonSmoothHint = GL_DONT_CARE;
    GLenum TextureCompressionHint = GL_DONT_CARE;
    GLenum FragmentShaderDerivativeHint = GL_DONT_CARE;

    // ---- Point parameters ----
    float PointFadeThresholdSize = 1.0f;
    GLenum PointSpriteCoordOrigin = GL_UPPER_LEFT;

    // ---- Color clamping (glClampColor). Core profile exposes only
    //      GL_CLAMP_READ_COLOR; default is GL_FIXED_ONLY. ----
    GLenum ClampReadColor = GL_FIXED_ONLY;

    // ---- Polygon rasterization mode (glPolygonMode). Core profile sets
    //      front and back together, but GL_POLYGON_MODE still reports both
    //      slots, so keep them separate for a faithful query. ----
    GLenum PolygonModeFront = GL_FILL;
    GLenum PolygonModeBack = GL_FILL;

    // ---- Primitive restart index (glPrimitiveRestartIndex). Default 0. ----
    uint32_t PrimitiveRestartIndex = 0;

    // ---- Capability enables (glEnable / glDisable). ----
    bool ColorLogicOpEnabled = false;
    bool DebugOutputEnabled = false;
    bool DebugOutputSynchronousEnabled = false;
    bool DepthClampEnabled = false;
    bool DitherEnabled = true;
    bool FramebufferSrgbEnabled = false;
    bool LineSmoothEnabled = false;
    bool MultisampleEnabled = true;
    bool PolygonOffsetFillEnabled = false;
    bool PolygonOffsetLineEnabled = false;
    bool PolygonOffsetPointEnabled = false;
    bool PolygonSmoothEnabled = false;
    bool PrimitiveRestartEnabled = false;
    bool PrimitiveRestartFixedIndexEnabled = false;
    bool RasterizerDiscardEnabled = false;
    bool SampleAlphaToCoverageEnabled = false;
    bool SampleAlphaToOneEnabled = false;
    bool SampleCoverageEnabled = false;
    bool SampleShadingEnabled = false;
    bool SampleMaskEnabled = false;
    bool ScissorTestEnabled = false;
    bool StencilTestEnabled = false;
    bool TextureCubeMapSeamlessEnabled = false;
    bool ProgramPointSizeEnabled = false;
    // GL_CLIP_DISTANCE0..7 (8 user clip planes).
    std::array<bool, 8> ClipDistanceEnabled{};

    // ---- Scissor box ----
    IntRect ScissorBox;                     // x, y, w, h
};

class RenderState {
public:
    RenderState();

    uint16_t GetVersion() const;
    const RenderStateParameters& GetAllParameters() const;

    // ---- Rasterization ----
    void SetViewport(int x, int y, int w, int h);
    IntRect GetViewport() const;
    void SetLineWidth(float width);
    float GetLineWidth() const;
    void SetPointSize(float size);
    float GetPointSize() const;
    void SetPolygonOffset(float factor, float units);
    float GetPolygonOffsetFactor() const;
    float GetPolygonOffsetUnits() const;

    // ---- Hints / point params / color clamp / polygon mode (GLenum passthrough) ----
    void SetHint(GLenum target, GLenum mode);
    GLenum GetHint(GLenum target) const;
    void SetPointFadeThresholdSize(float size);
    float GetPointFadeThresholdSize() const;
    void SetPointSpriteCoordOrigin(GLenum origin);
    GLenum GetPointSpriteCoordOrigin() const;
    void SetClampReadColor(GLenum clamp);
    GLenum GetClampReadColor() const;
    void SetPolygonMode(GLenum front, GLenum back);
    GLenum GetPolygonModeFront() const;
    GLenum GetPolygonModeBack() const;
    void SetPrimitiveRestartIndex(uint32_t index);
    uint32_t GetPrimitiveRestartIndex() const;

    // ---- Capabilities ----
    void SetCapability(CapabilityInput cap, bool enabled);
    bool IsCapabilityEnabled(CapabilityInput cap) const;

    // ---- Blending ----
    void SetBlendFunc(BlendFactor srcRGB, BlendFactor dstRGB,
                      BlendFactor srcAlpha, BlendFactor dstAlpha);
    void GetBlendFunc(BlendFactor& srcRGB, BlendFactor& dstRGB,
                      BlendFactor& srcAlpha, BlendFactor& dstAlpha) const;
    void SetBlendFuncIndexed(uint32_t index, BlendFactor srcRGB, BlendFactor dstRGB,
                             BlendFactor srcAlpha, BlendFactor dstAlpha);
    void GetBlendFuncIndexed(uint32_t index, BlendFactor& srcRGB, BlendFactor& dstRGB,
                             BlendFactor& srcAlpha, BlendFactor& dstAlpha) const;
    void SetBlendEquation(BlendEquation color, BlendEquation alpha);
    void GetBlendEquation(BlendEquation& color, BlendEquation& alpha) const;
    void SetBlendEquationIndexed(uint32_t index, BlendEquation color, BlendEquation alpha);
    void GetBlendEquationIndexed(uint32_t index, BlendEquation& color, BlendEquation& alpha) const;
    void SetBlendColor(const float color[4]);
    const float* GetBlendColor() const;
    void SetLogicOp(LogicOperation op);
    LogicOperation GetLogicOp() const;

    // ---- Depth ----
    void SetDepthFunc(DepthTestFunc func);
    DepthTestFunc GetDepthFunc() const;
    void SetDepthMask(bool flag);
    bool GetDepthMask() const;
    void SetDepthRange(float n, float f);
    void GetDepthRange(float& n, float& f) const;

    // ---- Stencil ----
    void SetStencilFunc(StencilFace face, DepthTestFunc func, int ref, uint32_t mask);
    void SetStencilMask(StencilFace face, uint32_t mask);
    void SetStencilOp(StencilFace face, StencilOperation fail,
                      StencilOperation dpfail, StencilOperation dppass);
    const StencilFaceState& GetStencilState(StencilFace face) const;

    // ---- Color mask ----
    void SetColorMask(BoolVec4 mask);
    BoolVec4 GetColorMask() const;
    void SetColorMaskIndexed(uint32_t index, BoolVec4 mask);
    BoolVec4 GetColorMaskIndexed(uint32_t index) const;

    // ---- Clear values ----
    void SetClearColor(const float color[4]);
    const float* GetClearColor() const;
    void SetClearDepth(float depth);
    float GetClearDepth() const;
    void SetClearStencil(uint32_t stencil);
    uint32_t GetClearStencil() const;

    // ---- Multisample coverage ----
    void SetSampleCoverage(float value, bool invert);
    float GetSampleCoverageValue() const;
    bool GetSampleCoverageInvert() const;
    void SetSampleMaskValue(uint32_t mask);
    uint32_t GetSampleMaskValue() const;

    // ---- Pixel store ----
    void SetPixelStoreParam(PixelStoreParam param, int value);
    int GetPixelStoreParam(PixelStoreParam param) const;
    PixelStoreParameters GetPixelStoreParameters(bool isUnpack) const;

    // ---- Cull face / front face ----
    void SetCullFaceMode(CullFaceMode mode);
    CullFaceMode GetCullFaceMode() const;
    void SetFrontFaceMode(FrontFaceMode mode);
    FrontFaceMode GetFrontFaceMode() const;

    // ---- Scissor ----
    void SetScissorBox(int x, int y, int w, int h);
    IntRect GetScissorBox() const;

private:
    uint16_t m_version = 0;
    RenderStateParameters m_parameters;

    // Pack / unpack pixel storage (kept outside m_parameters, matching the
    // historical split: pack applies to glReadPixels, unpack to glTexImage*).
    PixelStoreParameters m_pixelStorePack;
    PixelStoreParameters m_pixelStoreUnpack;
};

} // namespace mithril::glstate
