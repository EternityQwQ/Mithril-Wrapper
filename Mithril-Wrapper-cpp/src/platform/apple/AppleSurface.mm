#import "platform/apple/AppleSurface.h"

#import <QuartzCore/CAMetalLayer.h>
#import <TargetConditionals.h>
#if TARGET_OS_IOS || TARGET_OS_TV
#import <UIKit/UIKit.h>
#else
#import <AppKit/AppKit.h>
#endif

#include <algorithm>

namespace mithril::platform::apple {

struct AppleSurface::Impl {
    CAMetalLayer* layer;
    std::uint64_t generation;
    std::uint32_t width;
    std::uint32_t height;
};

namespace {

CAMetalLayer* resolveLayer(void* nativeWindow) {
    if (nativeWindow == nullptr) return [CAMetalLayer layer];
    id object = (__bridge id)nativeWindow;
    if ([object isKindOfClass:[CAMetalLayer class]]) return (CAMetalLayer*)object;
#if TARGET_OS_IOS || TARGET_OS_TV
    if ([object isKindOfClass:[UIView class]]) {
        UIView* view = (UIView*)object;
        if ([view.layer isKindOfClass:[CAMetalLayer class]]) return (CAMetalLayer*)view.layer;
        // A UIView's layer class is fixed at construction time. Replacing or
        // mutating it here is unsafe; the host must provide a Metal-backed view.
        return nil;
    }
#else
    if ([object isKindOfClass:[NSView class]]) {
        NSView* view = (NSView*)object;
        view.wantsLayer = YES;
        if ([view.layer isKindOfClass:[CAMetalLayer class]]) return (CAMetalLayer*)view.layer;
        return nil;
    }
#endif
    // Plain CALayer objects are rejected. The host owns the view hierarchy and
    // must pass the exact CAMetalLayer that represents the drawable surface.
    if ([object isKindOfClass:[CALayer class]]) return nil;
    return nil;
}

void configure(CAMetalLayer* layer, const backend::SurfaceDesc& desc,
               std::uint32_t& width, std::uint32_t& height) {
    layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    layer.framebufferOnly = desc.framebufferOnly;
    layer.opaque = YES;
    CGFloat scale = std::max(0.01F, desc.scale);
    if (desc.nativeWindow != nullptr && desc.scale == 1.0F && layer.contentsScale > 1.0) {
        scale = layer.contentsScale;
    }
    width = desc.width != 0 ? desc.width : static_cast<std::uint32_t>(std::max(1.0, layer.bounds.size.width));
    height = desc.height != 0 ? desc.height : static_cast<std::uint32_t>(std::max(1.0, layer.bounds.size.height));
    layer.contentsScale = scale;
    layer.drawableSize = CGSizeMake(static_cast<CGFloat>(width) * scale,
                                    static_cast<CGFloat>(height) * scale);
    if (@available(iOS 11.2, macOS 10.13.2, *)) layer.maximumDrawableCount = 3;
    if (@available(iOS 13.0, macOS 10.15, *)) layer.allowsNextDrawableTimeout = YES;
}

} // namespace

AppleSurface::AppleSurface() noexcept = default;
AppleSurface::AppleSurface(std::unique_ptr<Impl> value) noexcept : impl_(std::move(value)) {}
AppleSurface::~AppleSurface() = default;
AppleSurface::AppleSurface(AppleSurface&&) noexcept = default;
AppleSurface& AppleSurface::operator=(AppleSurface&&) noexcept = default;

core::ValueResult<AppleSurface> AppleSurface::create(const backend::SurfaceDesc& desc) {
    @autoreleasepool {
        if ((desc.width == 0 || desc.height == 0) && desc.nativeWindow == nullptr) {
            return core::ValueResult<AppleSurface>::failure(core::Error::make(
                core::ErrorDomain::surface, core::ErrorCode::invalid_argument,
                "surface dimensions must be non-zero"));
        }
        CAMetalLayer* layer = resolveLayer(desc.nativeWindow);
        if (layer == nil) {
            return core::ValueResult<AppleSurface>::failure(core::Error::make(
                core::ErrorDomain::surface, core::ErrorCode::invalid_argument,
                "native window cannot provide a Metal layer"));
        }
        auto impl = std::make_unique<Impl>();
        impl->layer = layer;
        impl->generation = desc.generation;
        configure(layer, desc, impl->width, impl->height);
        return AppleSurface(std::move(impl));
    }
}

core::Result AppleSurface::resize(const backend::SurfaceDesc& desc) {
    if (!impl_ || ((desc.width == 0 || desc.height == 0) && desc.nativeWindow == nullptr)) {
        return core::Result::failure(core::Error::make(core::ErrorDomain::surface,
            core::ErrorCode::invalid_argument, "invalid surface resize"));
    }
    configure(impl_->layer, desc, impl_->width, impl_->height);
    impl_->generation = desc.generation;
    return {};
}

void* AppleSurface::nextDrawable() {
    if (!impl_) return nullptr;
    return (__bridge_retained void*)[impl_->layer nextDrawable];
}

void* AppleSurface::layer() const noexcept {
    return impl_ ? (__bridge void*)impl_->layer : nullptr;
}

std::uint64_t AppleSurface::generation() const noexcept {
    return impl_ ? impl_->generation : 0;
}

std::uint32_t AppleSurface::width() const noexcept { return impl_ ? impl_->width : 0; }
std::uint32_t AppleSurface::height() const noexcept { return impl_ ? impl_->height : 0; }

} // namespace mithril::platform::apple
