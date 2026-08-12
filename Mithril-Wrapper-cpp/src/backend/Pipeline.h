#pragma once

#include "backend/ResourceTypes.h"
#include "ir/PipelineKey.h"

#include <vector>

namespace mithril::backend {

struct PipelineDesc {
    ir::PipelineKey key;
    std::vector<VertexAttributeDesc> vertexAttributes;
    bool alphaToCoverage{};
};

} // namespace mithril::backend
