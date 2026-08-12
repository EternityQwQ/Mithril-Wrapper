#pragma once

#include "backend/Pipeline.h"
#include "core/Handles.h"
#include "core/Result.h"
#include "shader/ShaderTypes.h"

#include <span>

namespace mithril::backend {

class ShaderCompilerDevice {
public:
    virtual ~ShaderCompilerDevice() = default;
    virtual core::ValueResult<core::ShaderHandle> createShader(const shader::MslArtifact&) = 0;
    virtual core::ValueResult<core::PipelineHandle> createPipeline(
        std::span<const core::ShaderHandle>, const PipelineDesc&) = 0;
    virtual core::Result release(core::ShaderHandle) = 0;
    virtual core::Result release(core::PipelineHandle) = 0;
};

} // namespace mithril::backend
