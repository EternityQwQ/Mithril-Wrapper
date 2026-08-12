#import "metal/MetalShaderLibraryCompiler.h"

#import <Metal/Metal.h>

#include <string_view>

namespace mithril::metal {

struct CompiledMetalShader::Impl {
    id<MTLLibrary> library;
    id<MTLFunction> function;
    shader::ShaderStage stage;
    std::uint64_t hash;
};

namespace {

std::uint64_t hashSource(std::string_view source) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (const unsigned char value : source) {
        hash ^= value;
        hash *= 1099511628211ULL;
    }
    return hash;
}

} // namespace

CompiledMetalShader::CompiledMetalShader(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
CompiledMetalShader::~CompiledMetalShader() = default;
shader::ShaderStage CompiledMetalShader::stage() const noexcept { return impl_->stage; }
std::uint64_t CompiledMetalShader::hash() const noexcept { return impl_->hash; }
void* CompiledMetalShader::nativeFunction() const noexcept {
    return (__bridge void*)impl_->function;
}

MetalShaderLibraryCompiler::MetalShaderLibraryCompiler(void* device) noexcept : device_(device) {}

core::ValueResult<std::shared_ptr<CompiledMetalShader>> MetalShaderLibraryCompiler::compile(
    const shader::MslArtifact& artifact) const {
    @autoreleasepool {
        id<MTLDevice> device = (__bridge id<MTLDevice>)device_;
        if (device == nil || artifact.source.empty() || artifact.entryPoint.empty()) {
            return core::ValueResult<std::shared_ptr<CompiledMetalShader>>::failure(core::Error::make(
                core::ErrorDomain::shader, core::ErrorCode::invalid_argument,
                "Metal shader source or entry point is empty"));
        }
        NSString* source = [[NSString alloc] initWithBytes:artifact.source.data()
            length:artifact.source.size() encoding:NSUTF8StringEncoding];
        MTLCompileOptions* options = [MTLCompileOptions new];
        if (@available(iOS 12.0, macOS 10.14, *)) options.languageVersion = MTLLanguageVersion2_0;
        NSError* error = nil;
        id<MTLLibrary> library = [device newLibraryWithSource:source options:options error:&error];
        if (library == nil) {
            const char* message = error.localizedDescription.UTF8String;
            return core::ValueResult<std::shared_ptr<CompiledMetalShader>>::failure(core::Error::make(
                core::ErrorDomain::shader, core::ErrorCode::compile_failed,
                message != nullptr ? message : "Metal shader compilation failed"));
        }
        NSString* entry = [[NSString alloc] initWithBytes:artifact.entryPoint.data()
            length:artifact.entryPoint.size() encoding:NSUTF8StringEncoding];
        id<MTLFunction> function = [library newFunctionWithName:entry];
        if (function == nil) {
            NSString* available = [library.functionNames componentsJoinedByString:@", "];
            NSString* detail = [NSString stringWithFormat:@"Metal entry '%@' was not found; available: %@",
                entry, available];
            return core::ValueResult<std::shared_ptr<CompiledMetalShader>>::failure(core::Error::make(
                core::ErrorDomain::shader, core::ErrorCode::not_found,
                detail.UTF8String != nullptr ? detail.UTF8String : "Metal shader entry point was not found"));
        }
        auto impl = std::make_unique<CompiledMetalShader::Impl>();
        impl->library = library;
        impl->function = function;
        impl->stage = artifact.stage;
        impl->hash = hashSource(artifact.source);
        return std::shared_ptr<CompiledMetalShader>(new CompiledMetalShader(std::move(impl)));
    }
}

} // namespace mithril::metal
