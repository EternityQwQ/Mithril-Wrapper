#import "MG_Backend/DirectMetal/AppleCapabilities.h"

#import <Metal/Metal.h>

namespace mithril::platform::apple {

core::ValueResult<gl::CapabilityManifest> AppleCapabilities::queryDefaultDevice() {
    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (device == nil) {
            return core::ValueResult<gl::CapabilityManifest>::failure(core::Error::make(
                core::ErrorDomain::device, core::ErrorCode::unavailable, "Metal device unavailable"));
        }

        gl::CapabilityManifest result;
        result.deviceName = device.name.UTF8String != nullptr ? device.name.UTF8String : "Apple GPU";
        result.maxTextureSize = 8192;
        result.maxColorAttachments = 8;
        result.argumentBuffers = false;
        result.sharedEvents = false;
        result.counterSampling = false;
        result.binaryArchives = false;

        if (@available(iOS 13.0, macOS 10.15, *)) {
            result.argumentBuffers = device.argumentBuffersSupport >= MTLArgumentBuffersTier1;
        }
        if (@available(iOS 12.0, macOS 10.14, *)) {
            result.sharedEvents = true;
        }
        if (@available(iOS 14.0, macOS 11.0, *)) {
            result.counterSampling = device.counterSets.count != 0;
            result.binaryArchives = true;
        }
        return result;
    }
}

} // namespace mithril::platform::apple
