#import "metal/MetalDeviceSession.h"

#import "metal/DeferredReleaseQueue.h"
#import "metal/FrameScheduler.h"
#import "metal/MetalShaderLibraryCompiler.h"
#import "platform/apple/AppleCapabilities.h"
#import "platform/apple/AppleSurface.h"

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <TargetConditionals.h>

#include "core/ObjectStore.h"

#include <array>
#include <cstring>
#include <mutex>
#include <unordered_map>

namespace mithril::metal {

namespace {

template <typename T>
std::shared_ptr<void> retainMetal(T object) {
    if (object == nil) return {};
    void* retained = (__bridge_retained void*)object;
    return std::shared_ptr<void>(retained, [](void* value) { CFRelease(value); });
}

template <typename T>
T bridgeMetal(const std::shared_ptr<void>& object) {
    return (__bridge T)object.get();
}

MTLPixelFormat metalFormat(backend::PixelFormat format) {
    switch (format) {
        case backend::PixelFormat::rgba8Unorm: return MTLPixelFormatRGBA8Unorm;
        case backend::PixelFormat::bgra8Unorm: return MTLPixelFormatBGRA8Unorm;
        case backend::PixelFormat::depth32Float: return MTLPixelFormatDepth32Float;
        case backend::PixelFormat::depth24Stencil8:
#if TARGET_OS_IOS || TARGET_OS_TV
            return MTLPixelFormatDepth32Float_Stencil8;
#else
            return MTLPixelFormatDepth24Unorm_Stencil8;
#endif
        case backend::PixelFormat::depth32FloatStencil8: return MTLPixelFormatDepth32Float_Stencil8;
        case backend::PixelFormat::none: return MTLPixelFormatInvalid;
    }
}

MTLCompareFunction compareFunction(ir::Compare compare) {
    switch (compare) {
        case ir::Compare::always: return MTLCompareFunctionAlways;
        case ir::Compare::less: return MTLCompareFunctionLess;
        case ir::Compare::lessEqual: return MTLCompareFunctionLessEqual;
        case ir::Compare::equal: return MTLCompareFunctionEqual;
        case ir::Compare::greater: return MTLCompareFunctionGreater;
    }
    return MTLCompareFunctionAlways;
}

MTLVertexFormat vertexFormat(const backend::VertexAttributeDesc& attribute) {
    const auto count = attribute.components;
    switch (attribute.scalar) {
        case backend::VertexScalar::float32:
            return count == 1 ? MTLVertexFormatFloat : count == 2 ? MTLVertexFormatFloat2 :
                count == 3 ? MTLVertexFormatFloat3 : MTLVertexFormatFloat4;
        case backend::VertexScalar::sint32:
            return count == 1 ? MTLVertexFormatInt : count == 2 ? MTLVertexFormatInt2 :
                count == 3 ? MTLVertexFormatInt3 : MTLVertexFormatInt4;
        case backend::VertexScalar::uint32:
            return count == 1 ? MTLVertexFormatUInt : count == 2 ? MTLVertexFormatUInt2 :
                count == 3 ? MTLVertexFormatUInt3 : MTLVertexFormatUInt4;
        case backend::VertexScalar::sint16:
            if (attribute.normalized) return count == 2 ? MTLVertexFormatShort2Normalized : MTLVertexFormatShort4Normalized;
            return count == 2 ? MTLVertexFormatShort2 : MTLVertexFormatShort4;
        case backend::VertexScalar::uint16:
            if (attribute.normalized) return count == 2 ? MTLVertexFormatUShort2Normalized : MTLVertexFormatUShort4Normalized;
            return count == 2 ? MTLVertexFormatUShort2 : MTLVertexFormatUShort4;
        case backend::VertexScalar::sint8:
            if (attribute.normalized) return count == 2 ? MTLVertexFormatChar2Normalized : MTLVertexFormatChar4Normalized;
            return count == 2 ? MTLVertexFormatChar2 : MTLVertexFormatChar4;
        case backend::VertexScalar::uint8:
            if (attribute.normalized) return count == 2 ? MTLVertexFormatUChar2Normalized : MTLVertexFormatUChar4Normalized;
            return count == 2 ? MTLVertexFormatUChar2 : MTLVertexFormatUChar4;
    }
    return MTLVertexFormatInvalid;
}

struct BufferResource { std::shared_ptr<void> object; std::size_t size{}; };
struct TextureResource { std::shared_ptr<void> object; backend::TextureDesc desc; };
struct SamplerResource { std::shared_ptr<void> object; };
struct ShaderResource { std::shared_ptr<CompiledMetalShader> shader; };
struct PipelineResource {
    std::shared_ptr<void> state;
    std::shared_ptr<void> depthState;
    std::uint64_t cacheKey{};
    std::uint32_t references{1};
    MTLPrimitiveType primitive{MTLPrimitiveTypeTriangle};
};
struct SurfaceResource { platform::apple::AppleSurface surface; };
struct DrawableFrame {
    backend::Frame publicFrame;
    std::shared_ptr<void> drawable;
    std::uint64_t reservedSerial{};
    bool submitted{};
};

} // namespace

struct MetalDeviceSession::Impl {
    std::shared_ptr<void> device;
    std::shared_ptr<void> queue;
    gl::CapabilityManifest capabilities;
    FrameScheduler scheduler{3};
    DeferredReleaseQueue deferred;
    core::ObjectStore<BufferResource, core::ObjectKind::buffer> buffers;
    core::ObjectStore<TextureResource, core::ObjectKind::texture> textures;
    core::ObjectStore<SamplerResource, core::ObjectKind::sampler> samplers;
    core::ObjectStore<ShaderResource, core::ObjectKind::shader> shaders;
    core::ObjectStore<PipelineResource, core::ObjectKind::pipeline> pipelines;
    core::ObjectStore<SurfaceResource, core::ObjectKind::surface> surfaces;
    std::mutex framesMutex;
    std::unordered_map<std::uint64_t, DrawableFrame> frames;
    std::mutex pipelineCacheMutex;
    std::unordered_map<std::uint64_t, core::PipelineHandle> pipelineCache;
};

class MetalCommandEncoder final : public backend::CommandEncoder {
public:
    MetalCommandEncoder(std::shared_ptr<MetalDeviceSession> session, std::uint64_t serial,
                        std::shared_ptr<void> commandBuffer)
        : session_(std::move(session)), serial_(serial), commandBuffer_(std::move(commandBuffer)) {}
    ~MetalCommandEncoder() override {
        if (!committed_) session_->impl_->scheduler.cancel(serial_);
    }

    core::Result beginRenderPass(const backend::RenderPassDesc& desc) override {
        if (encoder_) return fail(core::ErrorCode::invalid_state, "render pass already active");
        auto color = session_->impl_->textures.get(desc.color);
        if (!color) return core::Result::failure(color.error());
        MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
        pass.colorAttachments[0].texture = bridgeMetal<id<MTLTexture>>(color.value()->object);
        pass.colorAttachments[0].loadAction = desc.clearColor ? MTLLoadActionClear : MTLLoadActionLoad;
        pass.colorAttachments[0].storeAction = MTLStoreActionStore;
        pass.colorAttachments[0].clearColor = MTLClearColorMake(
            desc.clearRed, desc.clearGreen, desc.clearBlue, desc.clearAlpha);
        if (desc.depthStencil.valid()) {
            auto depth = session_->impl_->textures.get(desc.depthStencil);
            if (!depth) return core::Result::failure(depth.error());
            id<MTLTexture> texture = bridgeMetal<id<MTLTexture>>(depth.value()->object);
            pass.depthAttachment.texture = texture;
            pass.depthAttachment.loadAction = desc.clearDepth ? MTLLoadActionClear : MTLLoadActionLoad;
            pass.depthAttachment.storeAction = MTLStoreActionStore;
            pass.depthAttachment.clearDepth = desc.clearDepthValue;
            if (depth.value()->desc.format == backend::PixelFormat::depth24Stencil8 ||
                depth.value()->desc.format == backend::PixelFormat::depth32FloatStencil8) {
                pass.stencilAttachment.texture = texture;
                pass.stencilAttachment.loadAction = desc.clearStencil ? MTLLoadActionClear : MTLLoadActionLoad;
                pass.stencilAttachment.storeAction = MTLStoreActionStore;
                pass.stencilAttachment.clearStencil = desc.clearStencilValue;
            }
        }
        id<MTLRenderCommandEncoder> encoder = [bridgeMetal<id<MTLCommandBuffer>>(commandBuffer_)
            renderCommandEncoderWithDescriptor:pass];
        if (encoder == nil) return fail(core::ErrorCode::unavailable, "Metal render encoder creation failed");
        encoder_ = retainMetal(encoder);
        return {};
    }

    core::Result bindPipeline(core::PipelineHandle handle) override {
        if (!encoder_) return fail(core::ErrorCode::invalid_state, "render pass is not active");
        auto pipeline = session_->impl_->pipelines.get(handle);
        if (!pipeline) return core::Result::failure(pipeline.error());
        id<MTLRenderCommandEncoder> encoder = bridgeMetal<id<MTLRenderCommandEncoder>>(encoder_);
        [encoder setRenderPipelineState:bridgeMetal<id<MTLRenderPipelineState>>(pipeline.value()->state)];
        if (pipeline.value()->depthState) {
            [encoder setDepthStencilState:bridgeMetal<id<MTLDepthStencilState>>(pipeline.value()->depthState)];
        }
        primitive_ = pipeline.value()->primitive;
        return {};
    }

    core::Result bindVertexBuffer(std::uint32_t slot, core::BufferHandle handle, std::size_t offset) override {
        auto buffer = session_->impl_->buffers.get(handle);
        if (!buffer) return core::Result::failure(buffer.error());
        if (!encoder_ || offset > buffer.value()->size) return fail(core::ErrorCode::invalid_argument, "invalid vertex buffer bind");
        [bridgeMetal<id<MTLRenderCommandEncoder>>(encoder_) setVertexBuffer:
            bridgeMetal<id<MTLBuffer>>(buffer.value()->object) offset:offset atIndex:slot];
        return {};
    }

    core::Result bindTexture(std::uint32_t slot, core::TextureHandle textureHandle,
                             core::SamplerHandle samplerHandle) override {
        auto texture = session_->impl_->textures.get(textureHandle);
        if (!texture) return core::Result::failure(texture.error());
        auto sampler = session_->impl_->samplers.get(samplerHandle);
        if (!sampler) return core::Result::failure(sampler.error());
        if (!encoder_) return fail(core::ErrorCode::invalid_state, "render pass is not active");
        auto encoder = bridgeMetal<id<MTLRenderCommandEncoder>>(encoder_);
        [encoder setFragmentTexture:bridgeMetal<id<MTLTexture>>(texture.value()->object) atIndex:slot];
        [encoder setFragmentSamplerState:bridgeMetal<id<MTLSamplerState>>(sampler.value()->object) atIndex:slot];
        return {};
    }

    core::Result draw(const backend::DrawCommand& command) override {
        if (!encoder_ || command.vertexCount == 0 || command.instanceCount == 0) {
            return fail(core::ErrorCode::invalid_argument, "invalid draw command");
        }
        [bridgeMetal<id<MTLRenderCommandEncoder>>(encoder_) drawPrimitives:primitive_
            vertexStart:command.vertexStart vertexCount:command.vertexCount instanceCount:command.instanceCount];
        return {};
    }

    core::Result endRenderPass() override {
        if (!encoder_) return fail(core::ErrorCode::invalid_state, "render pass is not active");
        [bridgeMetal<id<MTLRenderCommandEncoder>>(encoder_) endEncoding];
        encoder_.reset();
        return {};
    }

    core::Result commit() override {
        if (committed_ || encoder_) return fail(core::ErrorCode::invalid_state, "command buffer cannot be committed");
        session_->impl_->scheduler.submit(commandBuffer_.get(), serial_);
        committed_ = true;
        return {};
    }

private:
    core::Result fail(core::ErrorCode code, const char* message) const {
        return core::Result::failure(core::Error::make(core::ErrorDomain::device, code, message));
    }
    std::shared_ptr<MetalDeviceSession> session_;
    std::uint64_t serial_{};
    std::shared_ptr<void> commandBuffer_;
    std::shared_ptr<void> encoder_;
    MTLPrimitiveType primitive_{MTLPrimitiveTypeTriangle};
    bool committed_{};
};

MetalDeviceSession::MetalDeviceSession(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
MetalDeviceSession::~MetalDeviceSession() {
    (void)waitIdle(5ULL * NSEC_PER_SEC);
}

core::ValueResult<std::shared_ptr<MetalDeviceSession>> MetalDeviceSession::create() {
    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (device == nil) return core::ValueResult<std::shared_ptr<MetalDeviceSession>>::failure(
            core::Error::make(core::ErrorDomain::device, core::ErrorCode::unavailable, "Metal 2 device unavailable"));
        id<MTLCommandQueue> queue = [device newCommandQueue];
        if (queue == nil) return core::ValueResult<std::shared_ptr<MetalDeviceSession>>::failure(
            core::Error::make(core::ErrorDomain::device, core::ErrorCode::unavailable, "Metal command queue unavailable"));
        auto capabilities = platform::apple::AppleCapabilities::queryDefaultDevice();
        if (!capabilities) return core::ValueResult<std::shared_ptr<MetalDeviceSession>>::failure(capabilities.error());
        auto impl = std::make_unique<Impl>();
        impl->device = retainMetal(device);
        impl->queue = retainMetal(queue);
        impl->capabilities = std::move(capabilities.value());
        return std::shared_ptr<MetalDeviceSession>(new MetalDeviceSession(std::move(impl)));
    }
}

const gl::CapabilityManifest& MetalDeviceSession::capabilities() const noexcept { return impl_->capabilities; }

core::ValueResult<std::unique_ptr<backend::CommandEncoder>> MetalDeviceSession::createCommandEncoder() {
    auto serial = impl_->scheduler.reserve();
    if (!serial) return core::ValueResult<std::unique_ptr<backend::CommandEncoder>>::failure(serial.error());
    id<MTLCommandBuffer> commandBuffer = [bridgeMetal<id<MTLCommandQueue>>(impl_->queue) commandBuffer];
    if (commandBuffer == nil) {
        impl_->scheduler.cancel(serial.value());
        return core::ValueResult<std::unique_ptr<backend::CommandEncoder>>::failure(core::Error::make(
            core::ErrorDomain::device, core::ErrorCode::unavailable, "Metal command buffer unavailable"));
    }
    impl_->deferred.collect(impl_->scheduler.completed());
    return std::unique_ptr<backend::CommandEncoder>(
        new MetalCommandEncoder(shared_from_this(), serial.value(), retainMetal(commandBuffer)));
}

core::Result MetalDeviceSession::waitIdle(std::uint64_t timeoutNanoseconds) {
    auto result = impl_->scheduler.waitIdle(timeoutNanoseconds);
    if (result) impl_->deferred.collect(impl_->scheduler.completed());
    return result;
}

core::ValueResult<std::vector<std::byte>> MetalDeviceSession::readbackRgba8(
    core::TextureHandle handle) {
    auto texture = impl_->textures.get(handle);
    if (!texture) return core::ValueResult<std::vector<std::byte>>::failure(texture.error());
    if (texture.value()->desc.format != backend::PixelFormat::rgba8Unorm &&
        texture.value()->desc.format != backend::PixelFormat::bgra8Unorm) {
        return core::ValueResult<std::vector<std::byte>>::failure(core::Error::make(
            core::ErrorDomain::resource, core::ErrorCode::unsupported,
            "readback supports RGBA8 and BGRA8 textures only"));
    }
    const std::size_t tightRow = static_cast<std::size_t>(texture.value()->desc.width) * 4U;
    const std::size_t alignedRow = (tightRow + 255U) & ~std::size_t{255U};
    const std::size_t stagingSize = alignedRow * texture.value()->desc.height;
    id<MTLBuffer> staging = [bridgeMetal<id<MTLDevice>>(impl_->device)
        newBufferWithLength:stagingSize options:MTLResourceStorageModeShared];
    id<MTLCommandBuffer> commandBuffer = [bridgeMetal<id<MTLCommandQueue>>(impl_->queue) commandBuffer];
    id<MTLBlitCommandEncoder> blit = [commandBuffer blitCommandEncoder];
    if (staging == nil || commandBuffer == nil || blit == nil) {
        return core::ValueResult<std::vector<std::byte>>::failure(core::Error::make(
            core::ErrorDomain::device, core::ErrorCode::unavailable,
            "Metal readback resources unavailable"));
    }
    [blit copyFromTexture:bridgeMetal<id<MTLTexture>>(texture.value()->object)
        sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0)
        sourceSize:MTLSizeMake(texture.value()->desc.width, texture.value()->desc.height, 1)
        toBuffer:staging destinationOffset:0 destinationBytesPerRow:alignedRow
        destinationBytesPerImage:stagingSize];
    [blit endEncoding];
    [commandBuffer commit];
    [commandBuffer waitUntilCompleted];
    if (commandBuffer.status == MTLCommandBufferStatusError) {
        const char* message = commandBuffer.error.localizedDescription.UTF8String;
        return core::ValueResult<std::vector<std::byte>>::failure(core::Error::make(
            core::ErrorDomain::device, core::ErrorCode::unavailable,
            message != nullptr ? message : "Metal readback command failed"));
    }
    std::vector<std::byte> output(tightRow * texture.value()->desc.height);
    const auto* source = static_cast<const std::byte*>(staging.contents);
    for (std::uint32_t row = 0; row < texture.value()->desc.height; ++row) {
        std::memcpy(output.data() + tightRow * row, source + alignedRow * row, tightRow);
    }
    return output;
}

core::ValueResult<backend::SurfaceDesc> MetalDeviceSession::describeSurface(
    core::SurfaceHandle handle) const {
    auto surface = impl_->surfaces.get(handle);
    if (!surface) return core::ValueResult<backend::SurfaceDesc>::failure(surface.error());
    backend::SurfaceDesc result;
    result.width = surface.value()->surface.width();
    result.height = surface.value()->surface.height();
    result.generation = surface.value()->surface.generation();
    result.nativeWindow = surface.value()->surface.layer();
    return result;
}

core::ValueResult<core::BufferHandle> MetalDeviceSession::createBuffer(const backend::BufferDesc& desc) {
    if (desc.size == 0) return core::ValueResult<core::BufferHandle>::failure(core::Error::make(
        core::ErrorDomain::resource, core::ErrorCode::invalid_argument, "buffer size must be non-zero"));
    id<MTLBuffer> buffer = [bridgeMetal<id<MTLDevice>>(impl_->device) newBufferWithLength:desc.size
        options:MTLResourceStorageModeShared];
    if (buffer == nil) return core::ValueResult<core::BufferHandle>::failure(core::Error::make(
        core::ErrorDomain::resource, core::ErrorCode::out_of_memory, "Metal buffer allocation failed"));
    return impl_->buffers.create(BufferResource{retainMetal(buffer), desc.size});
}

core::ValueResult<core::TextureHandle> MetalDeviceSession::createTexture(const backend::TextureDesc& desc) {
    if (desc.width == 0 || desc.height == 0 || desc.mipLevels == 0 || desc.format == backend::PixelFormat::none) {
        return core::ValueResult<core::TextureHandle>::failure(core::Error::make(
            core::ErrorDomain::resource, core::ErrorCode::invalid_argument, "invalid texture description"));
    }
    MTLTextureDescriptor* descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:metalFormat(desc.format)
        width:desc.width height:desc.height mipmapped:desc.mipLevels > 1];
    descriptor.mipmapLevelCount = desc.mipLevels;
    descriptor.storageMode = MTLStorageModePrivate;
    descriptor.usage = MTLTextureUsageShaderRead | MTLTextureUsageRenderTarget;
    id<MTLTexture> texture = [bridgeMetal<id<MTLDevice>>(impl_->device) newTextureWithDescriptor:descriptor];
    if (texture == nil) return core::ValueResult<core::TextureHandle>::failure(core::Error::make(
        core::ErrorDomain::resource, core::ErrorCode::out_of_memory, "Metal texture allocation failed"));
    return impl_->textures.create(TextureResource{retainMetal(texture), desc});
}

core::ValueResult<core::SamplerHandle> MetalDeviceSession::createSampler(const backend::SamplerDesc& desc) {
    MTLSamplerDescriptor* descriptor = [MTLSamplerDescriptor new];
    descriptor.minFilter = desc.minFilter == backend::Filter::linear ? MTLSamplerMinMagFilterLinear : MTLSamplerMinMagFilterNearest;
    descriptor.magFilter = desc.magFilter == backend::Filter::linear ? MTLSamplerMinMagFilterLinear : MTLSamplerMinMagFilterNearest;
    const auto address = [](backend::AddressMode mode) {
        if (mode == backend::AddressMode::repeat) return MTLSamplerAddressModeRepeat;
        if (mode == backend::AddressMode::mirroredRepeat) return MTLSamplerAddressModeMirrorRepeat;
        return MTLSamplerAddressModeClampToEdge;
    };
    descriptor.sAddressMode = address(desc.addressU);
    descriptor.tAddressMode = address(desc.addressV);
    id<MTLSamplerState> sampler = [bridgeMetal<id<MTLDevice>>(impl_->device) newSamplerStateWithDescriptor:descriptor];
    if (sampler == nil) return core::ValueResult<core::SamplerHandle>::failure(core::Error::make(
        core::ErrorDomain::resource, core::ErrorCode::out_of_memory, "Metal sampler allocation failed"));
    return impl_->samplers.create(SamplerResource{retainMetal(sampler)});
}

core::Result MetalDeviceSession::upload(core::BufferHandle handle, std::size_t offset,
                                        std::span<const std::byte> bytes) {
    auto buffer = impl_->buffers.get(handle);
    if (!buffer) return core::Result::failure(buffer.error());
    if (offset > buffer.value()->size || bytes.size() > buffer.value()->size - offset) {
        return core::Result::failure(core::Error::make(core::ErrorDomain::resource,
            core::ErrorCode::invalid_argument, "buffer upload is out of bounds"));
    }
    std::memcpy(static_cast<std::byte*>(bridgeMetal<id<MTLBuffer>>(buffer.value()->object).contents) + offset,
                bytes.data(), bytes.size());
    return {};
}

template <typename Store, typename Handle>
core::Result retireObject(Store& store, Handle handle, DeferredReleaseQueue& deferred, std::uint64_t serial) {
    auto value = store.erase(handle);
    if (!value) return core::Result::failure(value.error());
    deferred.retire(serial, std::static_pointer_cast<void>(value.value()));
    return {};
}

core::Result MetalDeviceSession::release(core::BufferHandle h) { return retireObject(impl_->buffers, h, impl_->deferred, impl_->scheduler.lastSubmitted()); }
core::Result MetalDeviceSession::release(core::TextureHandle h) { return retireObject(impl_->textures, h, impl_->deferred, impl_->scheduler.lastSubmitted()); }
core::Result MetalDeviceSession::release(core::SamplerHandle h) { return retireObject(impl_->samplers, h, impl_->deferred, impl_->scheduler.lastSubmitted()); }

core::ValueResult<core::ShaderHandle> MetalDeviceSession::createShader(const shader::MslArtifact& artifact) {
    MetalShaderLibraryCompiler compiler(impl_->device.get());
    auto compiled = compiler.compile(artifact);
    if (!compiled) return core::ValueResult<core::ShaderHandle>::failure(compiled.error());
    return impl_->shaders.create(ShaderResource{std::move(compiled.value())});
}

core::ValueResult<core::PipelineHandle> MetalDeviceSession::createPipeline(
    std::span<const core::ShaderHandle> handles, const backend::PipelineDesc& desc) {
    std::shared_ptr<CompiledMetalShader> vertex;
    std::shared_ptr<CompiledMetalShader> fragment;
    for (const auto handle : handles) {
        auto shader = impl_->shaders.get(handle);
        if (!shader) return core::ValueResult<core::PipelineHandle>::failure(shader.error());
        if (shader.value()->shader->stage() == shader::ShaderStage::vertex) vertex = shader.value()->shader;
        else fragment = shader.value()->shader;
    }
    if (!vertex || !fragment) return core::ValueResult<core::PipelineHandle>::failure(core::Error::make(
        core::ErrorDomain::shader, core::ErrorCode::invalid_argument, "pipeline requires vertex and fragment shaders"));
    ir::PipelineKey key = desc.key.normalized();
    key.vertexShaderHash = vertex->hash();
    key.fragmentShaderHash = fragment->hash();
    std::uint64_t layoutHash = 1469598103934665603ULL;
    const auto mixLayout = [&layoutHash](std::uint64_t value) {
        layoutHash ^= value;
        layoutHash *= 1099511628211ULL;
    };
    for (const auto& attribute : desc.vertexAttributes) {
        mixLayout(attribute.location); mixLayout(attribute.bufferSlot); mixLayout(attribute.offset);
        mixLayout(attribute.stride); mixLayout(static_cast<std::uint8_t>(attribute.scalar));
        mixLayout(attribute.components); mixLayout(attribute.normalized);
    }
    key.vertexLayoutHash = layoutHash;
    const std::uint64_t cacheKey = ir::hashPipelineKey(key);
    {
        std::lock_guard lock(impl_->pipelineCacheMutex);
        auto found = impl_->pipelineCache.find(cacheKey);
        if (found != impl_->pipelineCache.end()) {
            auto cached = impl_->pipelines.get(found->second);
            if (cached) {
                ++cached.value()->references;
                return found->second;
            }
        }
    }
    MTLRenderPipelineDescriptor* descriptor = [MTLRenderPipelineDescriptor new];
    descriptor.vertexFunction = vertex->impl_->function;
    descriptor.fragmentFunction = fragment->impl_->function;
    descriptor.sampleCount = key.sampleCount;
    descriptor.alphaToCoverageEnabled = desc.alphaToCoverage;
    for (std::uint8_t index = 0; index < key.colorAttachmentCount; ++index) {
        descriptor.colorAttachments[index].pixelFormat = metalFormat(key.colorFormats[index]);
        descriptor.colorAttachments[index].blendingEnabled = key.blending;
        if (key.blending) {
            descriptor.colorAttachments[index].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
            descriptor.colorAttachments[index].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
            descriptor.colorAttachments[index].sourceAlphaBlendFactor = MTLBlendFactorOne;
            descriptor.colorAttachments[index].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        }
    }
    const MTLPixelFormat depthFormat = metalFormat(key.depthStencilFormat);
    if (depthFormat != MTLPixelFormatInvalid) {
        descriptor.depthAttachmentPixelFormat = depthFormat;
        if (key.depthStencilFormat == backend::PixelFormat::depth24Stencil8 ||
            key.depthStencilFormat == backend::PixelFormat::depth32FloatStencil8) {
            descriptor.stencilAttachmentPixelFormat = depthFormat;
        }
    }
    if (!desc.vertexAttributes.empty()) {
        MTLVertexDescriptor* vertexDescriptor = [MTLVertexDescriptor vertexDescriptor];
        for (const auto& attribute : desc.vertexAttributes) {
            if (attribute.location >= 31 || attribute.bufferSlot >= 31 || attribute.components == 0 || attribute.components > 4) {
                return core::ValueResult<core::PipelineHandle>::failure(core::Error::make(
                    core::ErrorDomain::resource, core::ErrorCode::invalid_argument, "unsupported vertex layout"));
            }
            vertexDescriptor.attributes[attribute.location].format = vertexFormat(attribute);
            vertexDescriptor.attributes[attribute.location].offset = attribute.offset;
            vertexDescriptor.attributes[attribute.location].bufferIndex = attribute.bufferSlot;
            vertexDescriptor.layouts[attribute.bufferSlot].stride = attribute.stride;
            vertexDescriptor.layouts[attribute.bufferSlot].stepFunction = MTLVertexStepFunctionPerVertex;
        }
        descriptor.vertexDescriptor = vertexDescriptor;
    }
    NSError* error = nil;
    id<MTLRenderPipelineState> pipeline = [bridgeMetal<id<MTLDevice>>(impl_->device)
        newRenderPipelineStateWithDescriptor:descriptor error:&error];
    if (pipeline == nil) {
        const char* message = error.localizedDescription.UTF8String;
        return core::ValueResult<core::PipelineHandle>::failure(core::Error::make(core::ErrorDomain::device,
            core::ErrorCode::compile_failed, message != nullptr ? message : "Metal pipeline creation failed"));
    }
    std::shared_ptr<void> depthState;
    if (depthFormat != MTLPixelFormatInvalid) {
        MTLDepthStencilDescriptor* depth = [MTLDepthStencilDescriptor new];
        depth.depthCompareFunction = compareFunction(key.depthCompare);
        depth.depthWriteEnabled = key.depthWrite;
        depthState = retainMetal([bridgeMetal<id<MTLDevice>>(impl_->device) newDepthStencilStateWithDescriptor:depth]);
    }
    MTLPrimitiveType primitive = MTLPrimitiveTypeTriangle;
    if (key.primitive == ir::Primitive::point) primitive = MTLPrimitiveTypePoint;
    else if (key.primitive == ir::Primitive::line) primitive = MTLPrimitiveTypeLine;
    auto result = impl_->pipelines.create(PipelineResource{
        retainMetal(pipeline), std::move(depthState), cacheKey, 1, primitive});
    if (result) {
        std::lock_guard lock(impl_->pipelineCacheMutex);
        impl_->pipelineCache[cacheKey] = result.value();
    }
    return result;
}

core::Result MetalDeviceSession::release(core::ShaderHandle h) { return retireObject(impl_->shaders, h, impl_->deferred, impl_->scheduler.lastSubmitted()); }
core::Result MetalDeviceSession::release(core::PipelineHandle h) {
    auto pipeline = impl_->pipelines.get(h);
    if (!pipeline) return core::Result::failure(pipeline.error());
    {
        std::lock_guard lock(impl_->pipelineCacheMutex);
        if (pipeline.value()->references > 1) {
            --pipeline.value()->references;
            return {};
        }
        auto found = impl_->pipelineCache.find(pipeline.value()->cacheKey);
        if (found != impl_->pipelineCache.end() && found->second == h) impl_->pipelineCache.erase(found);
    }
    return retireObject(impl_->pipelines, h, impl_->deferred, impl_->scheduler.lastSubmitted());
}

core::ValueResult<core::SurfaceHandle> MetalDeviceSession::createSurface(const backend::SurfaceDesc& desc) {
    auto surface = platform::apple::AppleSurface::create(desc);
    if (!surface) return core::ValueResult<core::SurfaceHandle>::failure(surface.error());
    auto handle = impl_->surfaces.create(SurfaceResource{std::move(surface.value())});
    if (handle) {
        auto stored = impl_->surfaces.get(handle.value());
        CAMetalLayer* layer = stored ? (__bridge CAMetalLayer*)stored.value()->surface.layer() : nil;
        layer.device = bridgeMetal<id<MTLDevice>>(impl_->device);
    }
    return handle;
}

core::Result MetalDeviceSession::resize(core::SurfaceHandle handle, const backend::SurfaceDesc& desc) {
    auto surface = impl_->surfaces.get(handle);
    return surface ? surface.value()->surface.resize(desc) : core::Result::failure(surface.error());
}

core::ValueResult<backend::Frame> MetalDeviceSession::acquire(core::SurfaceHandle handle) {
    auto surface = impl_->surfaces.get(handle);
    if (!surface) return core::ValueResult<backend::Frame>::failure(surface.error());
    void* retainedDrawable = surface.value()->surface.nextDrawable();
    if (retainedDrawable == nullptr) return core::ValueResult<backend::Frame>::failure(core::Error::make(
        core::ErrorDomain::surface, core::ErrorCode::unavailable, "Metal drawable unavailable"));
    auto serial = impl_->scheduler.reserve();
    if (!serial) { CFRelease(retainedDrawable); return core::ValueResult<backend::Frame>::failure(serial.error()); }
    id<CAMetalDrawable> drawable = (__bridge_transfer id<CAMetalDrawable>)retainedDrawable;
    id<MTLTexture> texture = drawable.texture;
    backend::TextureDesc textureDesc{static_cast<std::uint32_t>(texture.width),
        static_cast<std::uint32_t>(texture.height), 1, backend::PixelFormat::bgra8Unorm};
    auto textureHandle = impl_->textures.create(TextureResource{retainMetal(texture), textureDesc});
    if (!textureHandle) { impl_->scheduler.cancel(serial.value()); return core::ValueResult<backend::Frame>::failure(textureHandle.error()); }
    backend::Frame frame{serial.value(), surface.value()->surface.generation(), textureHandle.value()};
    std::lock_guard lock(impl_->framesMutex);
    impl_->frames.emplace(frame.serial, DrawableFrame{frame, retainMetal(drawable), serial.value(), false});
    return frame;
}

core::Result MetalDeviceSession::present(const backend::Frame& frame) {
    DrawableFrame stored;
    {
        std::lock_guard lock(impl_->framesMutex);
        auto found = impl_->frames.find(frame.serial);
        if (found == impl_->frames.end() || found->second.publicFrame.drawable != frame.drawable) {
            return core::Result::failure(core::Error::make(core::ErrorDomain::surface,
                core::ErrorCode::not_found, "presented frame does not exist"));
        }
        stored = std::move(found->second);
        impl_->frames.erase(found);
    }
    id<MTLCommandBuffer> commandBuffer = [bridgeMetal<id<MTLCommandQueue>>(impl_->queue) commandBuffer];
    if (commandBuffer == nil) { impl_->scheduler.cancel(stored.reservedSerial); return core::Result::failure(
        core::Error::make(core::ErrorDomain::device, core::ErrorCode::unavailable, "present command buffer unavailable")); }
    [commandBuffer presentDrawable:bridgeMetal<id<CAMetalDrawable>>(stored.drawable)];
    auto texture = impl_->textures.erase(frame.drawable);
    if (texture) impl_->deferred.retire(stored.reservedSerial, std::static_pointer_cast<void>(texture.value()));
    impl_->deferred.retire(stored.reservedSerial, stored.drawable);
    impl_->scheduler.submit((__bridge void*)commandBuffer, stored.reservedSerial);
    return {};
}

core::Result MetalDeviceSession::release(core::SurfaceHandle h) { return retireObject(impl_->surfaces, h, impl_->deferred, impl_->scheduler.lastSubmitted()); }

} // namespace mithril::metal
