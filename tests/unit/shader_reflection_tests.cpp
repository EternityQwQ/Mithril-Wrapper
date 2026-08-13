#include "shader/GlslangCompiler.h"
#include "shader/SpirvCrossMslCompiler.h"

#include <iostream>

int main() {
    const mithril::shader::ShaderSource shaders[] = {
        {mithril::shader::ShaderStage::vertex,
         "#version 330 core\n"
         "layout(location=0) in vec3 Position;\n"
         "uniform mat4 ModelViewMat;\n"
         "uniform vec4 ColorModulator;\n"
         "void main(){ gl_Position = ModelViewMat * vec4(Position, 1.0); }\n",
         "probe.vert", "main"},
        {mithril::shader::ShaderStage::fragment,
         "#version 330 core\n"
         "uniform sampler2D Sampler0;\n"
         "uniform vec4 ColorModulator;\n"
         "out vec4 fragColor;\n"
         "void main(){ fragColor = texture(Sampler0, vec2(0.5)) * ColorModulator; }\n",
         "probe.frag", "main"},
    };
    for (const auto& shader : shaders) {
        mithril::shader::GlslangCompiler glslang;
        auto spirv = glslang.compile(shader, {330, true});
        if (!spirv) { std::cerr << spirv.error().message << '\n'; return 1; }
        mithril::shader::SpirvCrossMslCompiler cross;
        auto msl = cross.translate(spirv.value());
        if (!msl) { std::cerr << msl.error().message << '\n'; return 1; }
        bool hasPlainUniform = false;
        for (const auto& binding : msl.value().bindings) {
            hasPlainUniform |= binding.kind == mithril::shader::BindingKind::plainUniform;
            if (binding.stage == mithril::shader::ShaderStage::vertex &&
                binding.kind == mithril::shader::BindingKind::plainUniform && binding.mslBuffer < 16U)
                return 2;
        }
        if (!hasPlainUniform) return 3;
    }
}
