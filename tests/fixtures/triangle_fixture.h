#pragma once

#include "shader/ShaderTypes.h"

namespace mithril::tests {

inline shader::ShaderSource triangleVertexShader() {
    return {shader::ShaderStage::vertex,
            "#version 330 core\nlayout(location=0) in vec2 position; "
            "layout(location=1) in vec3 color; out vec3 vertexColor; "
            "void main(){ gl_Position=vec4(position,0,1); vertexColor=color; }",
            "triangle.vert", "main"};
}

inline shader::ShaderSource triangleFragmentShader() {
    return {shader::ShaderStage::fragment,
            "#version 330 core\nin vec3 vertexColor; layout(location=0) out vec4 color; "
            "void main(){ color=vec4(vertexColor,1); }",
            "triangle.frag", "main"};
}

} // namespace mithril::tests
