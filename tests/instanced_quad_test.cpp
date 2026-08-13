/*
 * instanced_quad_test.cpp — Mithril-Wrapper glVertexAttribDivisor 端到端冒烟。
 *
 * 目的：验证 T2 修复链路完整（attrib.divisor + bindings[].divisor 双写 →
 * Pipeline.cpp::attrib_divisor 读出 → inputRate → vkCmdDraw 步进）。
 *
 * 思路：画 4x4 = 16 个实例（glDrawArraysInstanced），用两条属性：
 *   - loc=0 aPos (vec2) : divisor=0，每顶点 1 步
 *   - loc=1 aOffset(vec2): divisor=1，每实例 1 步
 *
 * fragment 把 vOffset 直接写出到 RGBA8 FBO：每个实例会染出一块位置不同
 * 的纯色块。glReadPixels 检查 4×4 实例栅格的中心像素是否符合预期色。
 *
 * 构建（语法校验）：
 *   clang++ -std=c++17 -fsyntax-only -Wall -Wextra \
 *     -IMithril-Wrapper-cpp/include tests/instanced_quad_test.cpp
 *
 * 运行（macOS 原生 / iOS sim，依赖 libmithril.dylib 已 dlopen）：
 *   clang++ -std=c++17 -O0 -Wall -Wextra \
 *     -IMithril-Wrapper-cpp/include \
 *     -o tests/instanced_quad_test tests/instanced_quad_test.cpp -ldl
 *   DYLD_LIBRARY_PATH="$(brew --prefix)/lib" \
 *     ./tests/instanced_quad_test build/libmithril.dylib
 */
#include <dlfcn.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include <GL/glcorearb.h>

#ifndef GL_VERTEX_ATTRIB_ARRAY_DIVISOR
#define GL_VERTEX_ATTRIB_ARRAY_DIVISOR 0x88FE
#endif
#ifndef GL_INVALID_INDEX
#define GL_INVALID_INDEX 0xFFFFFFFFu
#endif

/* ---- 入口指针 typedef（命名约定：funcName_fn 对应变量 funcName）---- */
typedef void    (*getError_fn)(void);
typedef void    (*viewport_fn)(GLint, GLint, GLsizei, GLsizei);
typedef void    (*clearColor_fn)(GLfloat, GLfloat, GLfloat, GLfloat);
typedef void    (*clear_fn)(GLbitfield);
typedef void    (*enableG_fn)(GLenum);
typedef void    (*genFramebuffers_fn)(GLsizei, GLuint*);
typedef void    (*bindFramebuffer_fn)(GLenum, GLuint);
typedef void    (*genTextures_fn)(GLsizei, GLuint*);
typedef void    (*bindTexture_fn)(GLenum, GLuint);
typedef void    (*texImage2D_fn)(GLenum, GLint, GLint, GLsizei, GLsizei,
                                 GLint, GLenum, GLenum, const void*);
typedef void    (*texParameteri_fn)(GLenum, GLenum, GLint);
typedef void    (*framebufferTexture2D_fn)(GLenum, GLenum, GLenum, GLuint, GLint);
typedef GLenum  (*checkFramebufferStatus_fn)(GLenum);
typedef void    (*genVertexArrays_fn)(GLsizei, GLuint*);
typedef void    (*bindVertexArray_fn)(GLuint);
typedef void    (*genBuffers_fn)(GLsizei, GLuint*);
typedef void    (*bindBuffer_fn)(GLenum, GLuint);
typedef void    (*bufferData_fn)(GLenum, GLsizeiptr, const void*, GLenum);
typedef void    (*vertexAttribPointer_fn)(GLuint, GLint, GLenum, GLboolean,
                                          GLsizei, const void*);
typedef void    (*enableVertexAttribArray_fn)(GLuint);
typedef void    (*vertexAttribDivisor_fn)(GLuint, GLuint);
typedef GLuint  (*createShader_fn)(GLenum);
typedef void    (*shaderSource_fn)(GLuint, GLsizei, const GLchar* const*,
                                   const GLint*);
typedef void    (*compileShader_fn)(GLuint);
typedef void    (*getShaderiv_fn)(GLuint, GLenum, GLint*);
typedef GLuint  (*createProgram_fn)(void);
typedef void    (*attachShader_fn)(GLuint, GLuint);
typedef void    (*linkProgram_fn)(GLuint);
typedef void    (*getProgramiv_fn)(GLuint, GLenum, GLint*);
typedef void    (*deleteShader_fn)(GLuint);
typedef void    (*deleteProgram_fn)(GLuint);
typedef void    (*useProgram_fn)(GLuint);
typedef void    (*drawArraysInstanced_fn)(GLenum, GLint, GLsizei, GLsizei);
typedef void    (*finish_fn)(void);
typedef void    (*readPixels_fn)(GLint, GLint, GLsizei, GLsizei,
                                 GLenum, GLenum, void*);
typedef void    (*getVertexAttribiv_fn)(GLuint, GLenum, GLint*);
typedef void    (*vertexAttribFormat_fn)(GLuint, GLint, GLenum, GLboolean, GLuint, GLuint);
typedef void    (*vertexAttribBinding_fn)(GLuint, GLuint);
typedef void    (*bindVertexBuffer_fn)(GLuint, GLuint, GLintptr, GLsizei);

static int failures = 0;
static int checks = 0;
#define CHECK(cond, fmt, ...) do {                                        \
    ++checks;                                                             \
    if (cond) { printf("ok   : " fmt "\n", ##__VA_ARGS__); }              \
    else      { printf("FAIL : " fmt "\n", ##__VA_ARGS__); ++failures; }  \
} while (0)

static void* open_libmithril(int argc, char** argv) {
    const char* candidates[4];
    int n = 0;
    if (argc > 1) candidates[n++] = argv[1];
    candidates[n++] = "./output/libmithril.dylib";
    candidates[n++] = "./build/libmithril.dylib";
    candidates[n++] = "./build/output/libmithril.dylib";
    for (int i = 0; i < n; ++i) {
        void* h = dlopen(candidates[i], RTLD_NOW | RTLD_GLOBAL);
        if (h) { printf("loaded: %s\n", candidates[i]); return h; }
    }
    fprintf(stderr, "dlopen failed (tried %d candidates)\n", n);
    return NULL;
}

#define RESOLVE(fn, sym) \
    fn = (fn##_fn)dlsym(h, sym); \
    if (!fn) { printf("FAIL: missing symbol %s\n", sym); ++failures; }

int main(int argc, char** argv) {
    void* h = open_libmithril(argc, argv);
    if (!h) return 2;

    getError_fn                  getError               = NULL;
    viewport_fn                  viewport               = NULL;
    clearColor_fn                clearColor             = NULL;
    clear_fn                     clear                  = NULL;
    enableG_fn                   enableG                = NULL;
    genFramebuffers_fn           genFramebuffers        = NULL;
    bindFramebuffer_fn           bindFramebuffer        = NULL;
    genTextures_fn               genTextures            = NULL;
    bindTexture_fn               bindTexture            = NULL;
    texImage2D_fn                texImage2D             = NULL;
    texParameteri_fn             texParameteri          = NULL;
    framebufferTexture2D_fn      framebufferTexture2D   = NULL;
    checkFramebufferStatus_fn    checkFramebufferStatus = NULL;
    genVertexArrays_fn           genVertexArrays        = NULL;
    bindVertexArray_fn           bindVertexArray        = NULL;
    genBuffers_fn                genBuffers             = NULL;
    bindBuffer_fn                bindBuffer             = NULL;
    bufferData_fn                bufferData             = NULL;
    vertexAttribPointer_fn       vertexAttribPointer    = NULL;
    enableVertexAttribArray_fn   enableVertexAttribArray= NULL;
    vertexAttribDivisor_fn       vertexAttribDivisor    = NULL;
    createShader_fn              createShader           = NULL;
    shaderSource_fn              shaderSource           = NULL;
    compileShader_fn             compileShader          = NULL;
    getShaderiv_fn               getShaderiv            = NULL;
    createProgram_fn             createProgram          = NULL;
    attachShader_fn              attachShader           = NULL;
    linkProgram_fn               linkProgram            = NULL;
    getProgramiv_fn              getProgramiv           = NULL;
    deleteShader_fn              deleteShader           = NULL;
    deleteProgram_fn             deleteProgram          = NULL;
    useProgram_fn                useProgram             = NULL;
    drawArraysInstanced_fn       drawArraysInstanced    = NULL;
    finish_fn                    finish                 = NULL;
    readPixels_fn                readPixels             = NULL;
    getVertexAttribiv_fn         getVertexAttribiv      = NULL;
    vertexAttribFormat_fn        vertexAttribFormat     = NULL;
    vertexAttribBinding_fn       vertexAttribBinding    = NULL;
    bindVertexBuffer_fn          bindVertexBuffer       = NULL;

    RESOLVE(getError, "glGetError");
    RESOLVE(viewport, "glViewport");
    RESOLVE(clearColor, "glClearColor");
    RESOLVE(clear, "glClear");
    RESOLVE(enableG, "glEnable");
    RESOLVE(genFramebuffers, "glGenFramebuffers");
    RESOLVE(bindFramebuffer, "glBindFramebuffer");
    RESOLVE(genTextures, "glGenTextures");
    RESOLVE(bindTexture, "glBindTexture");
    RESOLVE(texImage2D, "glTexImage2D");
    RESOLVE(texParameteri, "glTexParameteri");
    RESOLVE(framebufferTexture2D, "glFramebufferTexture2D");
    RESOLVE(checkFramebufferStatus, "glCheckFramebufferStatus");
    RESOLVE(genVertexArrays, "glGenVertexArrays");
    RESOLVE(bindVertexArray, "glBindVertexArray");
    RESOLVE(genBuffers, "glGenBuffers");
    RESOLVE(bindBuffer, "glBindBuffer");
    RESOLVE(bufferData, "glBufferData");
    RESOLVE(vertexAttribPointer, "glVertexAttribPointer");
    RESOLVE(enableVertexAttribArray, "glEnableVertexAttribArray");
    RESOLVE(vertexAttribDivisor, "glVertexAttribDivisor");
    RESOLVE(createShader, "glCreateShader");
    RESOLVE(shaderSource, "glShaderSource");
    RESOLVE(compileShader, "glCompileShader");
    RESOLVE(getShaderiv, "glGetShaderiv");
    RESOLVE(createProgram, "glCreateProgram");
    RESOLVE(attachShader, "glAttachShader");
    RESOLVE(linkProgram, "glLinkProgram");
    RESOLVE(getProgramiv, "glGetProgramiv");
    RESOLVE(deleteShader, "glDeleteShader");
    RESOLVE(deleteProgram, "glDeleteProgram");
    RESOLVE(useProgram, "glUseProgram");
    RESOLVE(drawArraysInstanced, "glDrawArraysInstanced");
    RESOLVE(finish, "glFinish");
    RESOLVE(readPixels, "glReadPixels");
    RESOLVE(getVertexAttribiv, "glGetVertexAttribiv");
    /* 可选（GL 4.3 separate format API）：失败时降级到 classic glVertexAttribPointer */
    vertexAttribFormat  = (vertexAttribFormat_fn)dlsym(h, "glVertexAttribFormat");
    vertexAttribBinding = (vertexAttribBinding_fn)dlsym(h, "glVertexAttribBinding");
    bindVertexBuffer    = (bindVertexBuffer_fn)dlsym(h, "glBindVertexBuffer");

    if (failures > 0) {
        printf("resolver: %d missing symbols → bailing out\n", failures);
        return 3;
    }

    /* --- T2.1 静态状态：glVertexAttribDivisor 写入 attribute + binding ----- */
    GLuint vao; genVertexArrays(1, &vao); bindVertexArray(vao);
    GLuint vbo; genBuffers(1, &vbo); bindBuffer(GL_ARRAY_BUFFER, vbo);

    const float tri[3*2] = {
        0.0f, 0.0f,
        1.0f, 0.0f,
        0.0f, 1.0f,
    };
    bufferData(GL_ARRAY_BUFFER, sizeof(tri), tri, GL_STATIC_DRAW);

    /* per-vertex attribute: 每个 instance 内部按顶点步进 */
    vertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2*sizeof(float), 0);
    enableVertexAttribArray(0);
    vertexAttribDivisor(0, 0);

    /* per-instance attribute: 装填到独立 VBO */
    GLuint instVbo; genBuffers(1, &instVbo);
    std::vector<float> offsets(16*2);
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            int i = (row*4 + col) * 2;
            offsets[i+0] = -0.75f + col * 0.5f;
            offsets[i+1] = -0.75f + row * 0.5f;
        }
    }
    if (vertexAttribFormat && vertexAttribBinding && bindVertexBuffer) {
        /* GL 4.3 separate format */
        vertexAttribFormat(1, 2, GL_FLOAT, GL_FALSE, 0, 0);
        vertexAttribBinding(1, 1);
        bindVertexBuffer(1, instVbo, 0, (GLsizei)(2*sizeof(float)));
        enableVertexAttribArray(1);
        vertexAttribDivisor(1, 1);
        CHECK(true, "used GL 4.3 separate format for per-instance attribute");
    } else {
        bindBuffer(GL_ARRAY_BUFFER, instVbo);
        bufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(offsets.size()*sizeof(float)),
                   offsets.data(), GL_STATIC_DRAW);
        vertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 2*sizeof(float), 0);
        enableVertexAttribArray(1);
        vertexAttribDivisor(1, 1);
        CHECK(true, "used classic glVertexAttribPointer for per-instance attribute");
    }

    /* T2.2 状态自检：getVertexAttribiv(DIVISOR) 返回 0/1 */
    GLint d0 = -1, d1 = -1;
    getVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_DIVISOR, &d0);
    getVertexAttribiv(1, GL_VERTEX_ATTRIB_ARRAY_DIVISOR, &d1);
    CHECK(d0 == 0, "attrib 0 divisor reflected as 0 (got %d)", d0);
    CHECK(d1 == 1, "attrib 1 divisor reflected as 1 (got %d)", d1);

    /* --- T2.3 渲染：4x4 实例栅格 → 离屏 FBO --- */
    GLuint fbo; genFramebuffers(1, &fbo); bindFramebuffer(GL_FRAMEBUFFER, fbo);
    GLuint tex; genTextures(1, &tex); bindTexture(GL_TEXTURE_2D, tex);
    texImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 256, 256, 0,
               GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    texParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    texParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    framebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                         GL_TEXTURE_2D, tex, 0);
    GLenum status = checkFramebufferStatus(GL_FRAMEBUFFER);
    CHECK(status == GL_FRAMEBUFFER_COMPLETE,
          "FBO complete (status=0x%X)", status);

    viewport(0, 0, 256, 256);
    clearColor(0, 0, 0, 1);
    clear(GL_COLOR_BUFFER_BIT);

    const char* vs =
        "#version 330 core\n"
        "layout(location=0) in vec2 aPos;\n"
        "layout(location=1) in vec2 aOffset;\n"
        "out vec2 vOff;\n"
        "void main(){\n"
        "  vOff = aOffset;\n"
        "  gl_Position = vec4(aPos * 0.25 + aOffset, 0.0, 1.0);\n"
        "}\n";
    const char* fs =
        "#version 330 core\n"
        "in vec2 vOff;\n"
        "out vec4 fragColor;\n"
        "void main(){\n"
        "  fragColor = vec4(vOff * 0.5 + 0.5, 0.0, 1.0);\n"
        "}\n";

    GLuint vsh = createShader(GL_VERTEX_SHADER);
    shaderSource(vsh, 1, &vs, nullptr);
    compileShader(vsh);
    GLint compiled = 0; getShaderiv(vsh, GL_COMPILE_STATUS, &compiled);
    CHECK(compiled != 0, "vertex shader compiled");

    GLuint fsh = createShader(GL_FRAGMENT_SHADER);
    shaderSource(fsh, 1, &fs, nullptr);
    compileShader(fsh);
    getShaderiv(fsh, GL_COMPILE_STATUS, &compiled);
    CHECK(compiled != 0, "fragment shader compiled");

    GLuint prog = createProgram();
    attachShader(prog, vsh);
    attachShader(prog, fsh);
    linkProgram(prog);
    GLint linked = 0; getProgramiv(prog, GL_LINK_STATUS, &linked);
    CHECK(linked != 0, "program linked");
    useProgram(prog);

    drawArraysInstanced(GL_TRIANGLES, 0, 3, 16);
    finish();

    /* 读回：检查 4x4 中心像素的 R/G 分量是否对应预期 (col, row) */
    std::vector<unsigned char> px(256*256*4);
    readPixels(0, 0, 256, 256, GL_RGBA, GL_UNSIGNED_BYTE, px.data());

    auto expected_r = [](int col) { return (unsigned char)(((-0.75f + col*0.5f) * 0.5f + 0.5f) * 255.0f); };
    auto expected_g = [](int row) { return (unsigned char)(((-0.75f + row*0.5f) * 0.5f + 0.5f) * 255.0f); };

    int total = 0, ok = 0;
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            int cx = 64 + col * 64;
            int cy = 192 - row * 64;
            if (cx >= 256) cx = 255; if (cy < 0) cy = 0;
            int idx = (cy * 256 + cx) * 4;
            unsigned char got_r = px[idx + 0];
            unsigned char got_g = px[idx + 1];
            unsigned char exp_r = expected_r(col);
            unsigned char exp_g = expected_g(row);
            ++total;
            if ((int)got_r >= (int)exp_r - 6 && (int)got_r <= (int)exp_r + 6 &&
                (int)got_g >= (int)exp_g - 6 && (int)got_g <= (int)exp_g + 6) {
                ++ok;
            } else if (failures < 8) {
                printf("  cell (col=%d,row=%d) at px(%d,%d): got R=%u G=%u, expected R=%u G=%u\n",
                       col, row, cx, cy, got_r, got_g, exp_r, exp_g);
            }
        }
    }
    CHECK(ok == total, "all %d instanced cells match expected color (%d matched)",
          total, ok);

    deleteShader(vsh);
    deleteShader(fsh);
    deleteProgram(prog);

    printf("\n=== instanced_quad_test: %d checks, %d failures ===\n", checks, failures);
    if (failures == 0) {
        printf("INSTANCED QUAD SMOKE ALL PASSED\n");
        return 0;
    }
    return 1;
}
