#include "abi/AbiManifest.h"
#include "abi/ProcTable.h"
#include "backend/CommandEncoder.h"
#include "core/ObjectStore.h"
#include "gl/Capability.h"
#include "gl/Context.h"
#include "ir/PipelineKey.h"
#include "shader/GlslPreprocessor.h"
#include "shader/ShaderTypes.h"
#include "fixtures/triangle_fixture.h"
#include "mocks/MockCommandEncoder.h"
#include "mocks/MockPresenter.h"
#include "mocks/MockResourceDevice.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

namespace {
int failures = 0;
void marker() {}
#define CHECK(condition) do { if (!(condition)) { std::cerr << "FAIL: " #condition " (line " << __LINE__ << ")\n"; ++failures; } } while (false)

void testExpectedAndStore() {
    mithril::core::ObjectStore<int, mithril::core::ObjectKind::buffer> store;
    auto first = store.create(42);
    CHECK(first);
    CHECK(store.get(first.value()));
    auto removed = store.erase(first.value());
    CHECK(removed && *removed.value() == 42);
    CHECK(!store.get(first.value()));
    auto second = store.create(7);
    CHECK(second && second.value().slot == first.value().slot);
    CHECK(second.value().generation != first.value().generation);
    CHECK(!store.get(first.value()));
}

void testContextAndErrors() {
    auto share = std::make_shared<mithril::gl::ShareGroup>();
    mithril::gl::Context context(share);
    CHECK(context.makeCurrent());
    CHECK(context.isCurrent());
    CHECK(mithril::gl::Context::current() == &context);
    context.pushError(mithril::core::Error::make(mithril::core::ErrorDomain::gl,
        mithril::core::ErrorCode::invalid_argument, "bad value"));
    CHECK(context.popError().code == mithril::core::ErrorCode::invalid_argument);
    CHECK(context.releaseCurrent());
    CHECK(!context.isCurrent());

    mithril::gl::Context first(share);
    mithril::gl::Context second(share);
    CHECK(first.makeCurrent());
    CHECK(second.makeCurrent());
    CHECK(second.isCurrent());
    CHECK(!first.isCurrent());
    CHECK(second.releaseCurrent());

    mithril::gl::Context occupied(share);
    CHECK(occupied.makeCurrent());
    bool otherThreadRejected = false;
    std::thread other([&] { otherThreadRejected = !occupied.makeCurrent(); });
    other.join();
    CHECK(otherThreadRejected);
    CHECK(occupied.releaseCurrent());
}

void testCapabilitiesAndPipelineKey() {
    mithril::gl::CapabilityManifest low;
    auto lowPlan = mithril::gl::chooseCapabilityPlan(low);
    CHECK(lowPlan.binding == mithril::gl::CapabilityPlan::BindingMode::fixedSlots);
    CHECK(lowPlan.synchronization == mithril::gl::CapabilityPlan::SyncMode::commandCompletion);
    CHECK(!lowPlan.exposeTimerQuery);
    low.argumentBuffers = low.sharedEvents = low.counterSampling = low.binaryArchives = true;
    auto highPlan = mithril::gl::chooseCapabilityPlan(low);
    CHECK(highPlan.binding == mithril::gl::CapabilityPlan::BindingMode::argumentBuffer);
    CHECK(highPlan.exposeTimerQuery);

    mithril::ir::PipelineKey key;
    key.colorAttachmentCount = 9;
    key.depthWrite = false;
    const auto normalized = key.normalized();
    CHECK(normalized.colorAttachmentCount == 4);
    CHECK(normalized.depthCompare == mithril::ir::Compare::less);
    CHECK(mithril::ir::hashPipelineKey(key) == mithril::ir::hashPipelineKey(normalized));
    auto readOnly = key;
    readOnly.depthCompare = mithril::ir::Compare::greater;
    CHECK(mithril::ir::hashPipelineKey(key) != mithril::ir::hashPipelineKey(readOnly));
}

void testMocksAndShader() {
    mithril::tests::MockResourceDevice resources;
    CHECK(resources.createBuffer({16, mithril::backend::BufferUsage::vertex}));
    mithril::tests::MockCommandEncoder commands;
    CHECK(commands.beginRenderPass({}).hasValue());
    CHECK(commands.draw({0, 3, 1}).hasValue());
    CHECK(commands.endRenderPass().hasValue());
    CHECK(commands.commit().hasValue());
    CHECK(commands.calls.size() == 4);

    auto source = mithril::tests::triangleVertexShader();
    auto prepared = mithril::shader::preprocessGlsl(source, {});
    CHECK(prepared);
    CHECK(prepared.value().find("#version 330 core") == 0);
    auto keyA = mithril::shader::makeShaderCacheKey(source, {});
    auto keyB = mithril::shader::makeShaderCacheKey(source, {});
    CHECK(keyA == keyB);
}

void testManifest() {
    std::ifstream file(std::string(MITHRIL_SOURCE_ROOT) + "/abi/manifest/gl-egl-manifest.json");
    CHECK(file.good());
    const std::string json{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    auto manifest = mithril::abi::AbiManifest::parse(json);
    CHECK(manifest);
    if (manifest) {
        CHECK(manifest.value().formatVersion() == "1.0");
        CHECK(manifest.value().find("eglGetError") != nullptr);
        CHECK(manifest.value().find("glBegin") != nullptr);
        CHECK(manifest.value().find("glBegin")->status == mithril::abi::SymbolStatus::unsupported);
        CHECK(manifest.value().find("glDrawElements") != nullptr);
    }
    const mithril::abi::ProcEntry entries[] = {{"supported", mithril::abi::SymbolStatus::implemented, &marker},
                                               {"unsupported", mithril::abi::SymbolStatus::unsupported, &marker}};
    mithril::abi::ProcTable table(entries);
    CHECK(table.lookup("supported") == &marker);
    CHECK(table.lookup("unsupported") == nullptr);
    CHECK(table.lookup("missing") == nullptr);
}
}

int main() {
    testExpectedAndStore();
    testContextAndErrors();
    testCapabilitiesAndPipelineKey();
    testMocksAndShader();
    testManifest();
    if (failures != 0) std::cerr << failures << " test assertions failed\n";
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
