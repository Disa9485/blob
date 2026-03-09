// GameRenderer.cpp
#include "GameRenderer.hpp"

#include <cmath>
#include <string>
#include <vector>

namespace render {

GameRenderer::~GameRenderer() {
    shutdown();
}

bool GameRenderer::initialize(std::string& error) {
    shutdown();

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glEnable(GL_MULTISAMPLE);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    static const char* VS_SRC = R"GLSL(
        #version 330 core
        layout (location = 0) in vec2 aPos;
        layout (location = 1) in vec2 aUV;
        uniform vec2 uScreen;
        out vec2 vUV;
        void main() {
            float x = (aPos.x / uScreen.x) * 2.0 - 1.0;
            float y = 1.0 - (aPos.y / uScreen.y) * 2.0;
            gl_Position = vec4(x, y, 0.0, 1.0);
            vUV = aUV;
        }
    )GLSL";

    static const char* FS_SRC = R"GLSL(
        #version 330 core
        in vec2 vUV;
        out vec4 FragColor;

        uniform sampler2D uTex;
        uniform vec2 uScreen;

        uniform int uLightingEnabled;
        uniform float uAmbientIntensity;
        uniform float uAmbientWarmth;
        uniform vec2 uLightPos;
        uniform vec3 uLightColor;
        uniform float uLightIntensity;
        uniform float uLightRadius;
        uniform float uLightSoftness;
        uniform float uVignetteStrength;

        void main() {
            vec4 tex = texture(uTex, vUV);

            if (tex.a <= 0.001) {
                discard;
            }

            vec3 color = tex.rgb;

            if (uLightingEnabled == 0) {
                FragColor = tex;
                return;
            }

            vec2 fragNorm = gl_FragCoord.xy / uScreen.xy;
            vec2 toLight = fragNorm - uLightPos;
            float dist = length(toLight);

            float radial = 1.0 - smoothstep(0.0, uLightRadius, dist);
            radial = pow(max(radial, 0.0), uLightSoftness);

            vec3 ambientTint = vec3(
                1.0,
                1.0 - 0.18 * uAmbientWarmth,
                1.0 - 0.32 * uAmbientWarmth
            );

            vec3 ambient = ambientTint * uAmbientIntensity;
            vec3 keyLight = uLightColor * (uLightIntensity * radial);

            vec2 centered = fragNorm * 2.0 - 1.0;
            float vignette = dot(centered, centered);
            float vignetteFactor = 1.0 - clamp(vignette * uVignetteStrength, 0.0, 0.75);

            vec3 lit = color * (ambient + keyLight);
            lit *= vignetteFactor;

            FragColor = vec4(lit, tex.a);
        }
    )GLSL";

    program_ = createProgram(VS_SRC, FS_SRC, error);
    if (!program_) {
        return false;
    }

    uScreenLoc_ = glGetUniformLocation(program_, "uScreen");
    uTexLoc_ = glGetUniformLocation(program_, "uTex");

    uLightingEnabledLoc_ = glGetUniformLocation(program_, "uLightingEnabled");
    uAmbientIntensityLoc_ = glGetUniformLocation(program_, "uAmbientIntensity");
    uAmbientWarmthLoc_ = glGetUniformLocation(program_, "uAmbientWarmth");
    uLightPosLoc_ = glGetUniformLocation(program_, "uLightPos");
    uLightColorLoc_ = glGetUniformLocation(program_, "uLightColor");
    uLightIntensityLoc_ = glGetUniformLocation(program_, "uLightIntensity");
    uLightRadiusLoc_ = glGetUniformLocation(program_, "uLightRadius");
    uLightSoftnessLoc_ = glGetUniformLocation(program_, "uLightSoftness");
    uVignetteStrengthLoc_ = glGetUniformLocation(program_, "uVignetteStrength");

    glGenVertexArrays(1, &quadVao_);
    glGenBuffers(1, &quadVbo_);
    glGenBuffers(1, &quadEbo_);

    glBindVertexArray(quadVao_);

    glBindBuffer(GL_ARRAY_BUFFER, quadVbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 16, nullptr, GL_DYNAMIC_DRAW);

    static const std::uint32_t quadIndices[6] = { 0, 1, 2, 2, 3, 0 };
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, quadEbo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(quadIndices), quadIndices, GL_STATIC_DRAW);

    const GLsizei stride = 4 * sizeof(float);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, (void*)(2 * sizeof(float)));

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    glUseProgram(program_);
    glUniform1i(uTexLoc_, 0);
    glUseProgram(0);

    backgroundTexture_ = 0;
    return true;
}

void GameRenderer::shutdown() {
    backgroundTexture_ = 0;

    if (quadEbo_) {
        glDeleteBuffers(1, &quadEbo_);
        quadEbo_ = 0;
    }
    if (quadVbo_) {
        glDeleteBuffers(1, &quadVbo_);
        quadVbo_ = 0;
    }
    if (quadVao_) {
        glDeleteVertexArrays(1, &quadVao_);
        quadVao_ = 0;
    }
    if (program_) {
        glDeleteProgram(program_);
        program_ = 0;
    }

    uScreenLoc_ = -1;
    uTexLoc_ = -1;
    framebufferWidth_ = 0;
    framebufferHeight_ = 0;
}

GLuint GameRenderer::compileShader(GLenum type, const char* src, std::string& error) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);

    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLint logLen = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLen);
        std::string log(static_cast<std::size_t>(logLen), '\0');
        glGetShaderInfoLog(shader, logLen, nullptr, log.data());
        error = log;
        glDeleteShader(shader);
        return 0;
    }

    return shader;
}

GLuint GameRenderer::createProgram(const char* vsSrc, const char* fsSrc, std::string& error) {
    GLuint vs = compileShader(GL_VERTEX_SHADER, vsSrc, error);
    if (!vs) {
        return 0;
    }

    GLuint fs = compileShader(GL_FRAGMENT_SHADER, fsSrc, error);
    if (!fs) {
        glDeleteShader(vs);
        return 0;
    }

    GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);

    GLint ok = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        GLint logLen = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logLen);
        std::string log(static_cast<std::size_t>(logLen), '\0');
        glGetProgramInfoLog(program, logLen, nullptr, log.data());
        error = log;
        glDeleteProgram(program);
        glDeleteShader(vs);
        glDeleteShader(fs);
        return 0;
    }

    glDeleteShader(vs);
    glDeleteShader(fs);
    return program;
}

void GameRenderer::setLightingConfig(const LightingConfig& config) {
    lighting_ = config;
}

GLuint GameRenderer::uploadTextureRGBA(const std::uint8_t* rgba, int w, int h) {
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);

    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}

void GameRenderer::beginFrame(int framebufferWidth, int framebufferHeight) {
    framebufferWidth_ = framebufferWidth;
    framebufferHeight_ = framebufferHeight;

    glViewport(0, 0, framebufferWidth_, framebufferHeight_);
    glClearColor(20.0f / 255.0f, 22.0f / 255.0f, 26.0f / 255.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(program_);
    glUniform2f(uScreenLoc_, static_cast<float>(framebufferWidth_), static_cast<float>(framebufferHeight_));
    glActiveTexture(GL_TEXTURE0);

    glUniform1i(uLightingEnabledLoc_, lighting_.enabled ? 1 : 0);
    glUniform1f(uAmbientIntensityLoc_, lighting_.ambient_intensity);
    glUniform1f(uAmbientWarmthLoc_, lighting_.ambient_warmth);
    glUniform2f(uLightPosLoc_, lighting_.light_x, lighting_.light_y);
    glUniform3f(uLightColorLoc_, lighting_.light_r, lighting_.light_g, lighting_.light_b);
    glUniform1f(uLightIntensityLoc_, lighting_.light_intensity);
    glUniform1f(uLightRadiusLoc_, lighting_.light_radius);
    glUniform1f(uLightSoftnessLoc_, lighting_.light_softness);
    glUniform1f(uVignetteStrengthLoc_, lighting_.vignette_strength);
}

void GameRenderer::setBackgroundTexture(GLuint texture) {
    backgroundTexture_ = texture;
}

void GameRenderer::clearBackgroundTexture() {
    backgroundTexture_ = 0;
}

void GameRenderer::renderBackground() {
    if (!backgroundTexture_ || framebufferWidth_ <= 0 || framebufferHeight_ <= 0) {
        return;
    }

    glBindTexture(GL_TEXTURE_2D, backgroundTexture_);
    glBindVertexArray(quadVao_);

    updateQuadVBO_AxisAligned(
        0.0f,
        0.0f,
        static_cast<float>(framebufferWidth_),
        static_cast<float>(framebufferHeight_),
        0.0f, 0.0f,
        1.0f, 1.0f
    );

    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr);
}

void GameRenderer::endFrame() {
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);
}

bool GameRenderer::initializeSoftRenderMesh(physics::RenderPart& part, std::string& error) {
    if (part.kind != physics::PartKind::Soft) {
        error = "initializeSoftRenderMesh called for non-soft part.";
        return false;
    }

    destroySoftRenderMesh(part);

    glGenVertexArrays(1, &part.softRender.vao);
    glGenBuffers(1, &part.softRender.vbo);
    glGenBuffers(1, &part.softRender.ebo);

    glBindVertexArray(part.softRender.vao);

    part.softRender.vdata.resize(part.soft.body.bodies.size() * 4);

    for (std::size_t i = 0; i < part.soft.body.bodies.size(); ++i) {
        const cpVect p = cpBodyGetPosition(part.soft.body.bodies[i]);
        part.softRender.vdata[i * 4 + 0] = static_cast<float>(p.x);
        part.softRender.vdata[i * 4 + 1] = static_cast<float>(p.y);
        part.softRender.vdata[i * 4 + 2] = part.soft.body.uvs[i].x;
        part.softRender.vdata[i * 4 + 3] = part.soft.body.uvs[i].y;
    }

    glBindBuffer(GL_ARRAY_BUFFER, part.softRender.vbo);
    glBufferData(
        GL_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(part.softRender.vdata.size() * sizeof(float)),
        part.softRender.vdata.data(),
        GL_DYNAMIC_DRAW
    );

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, part.softRender.ebo);
    glBufferData(
        GL_ELEMENT_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(part.soft.body.indices.size() * sizeof(std::uint32_t)),
        part.soft.body.indices.data(),
        GL_STATIC_DRAW
    );

    const GLsizei stride = 4 * sizeof(float);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, (void*)(2 * sizeof(float)));

    glBindVertexArray(0);

    part.softRender.indexCount = part.soft.body.indices.size();
    return true;
}

void GameRenderer::updateSoftRenderMesh(physics::RenderPart& part) {
    if (part.kind != physics::PartKind::Soft || !part.softRender.vbo) {
        return;
    }

    for (std::size_t i = 0; i < part.soft.body.bodies.size(); ++i) {
        const cpVect p = cpBodyGetPosition(part.soft.body.bodies[i]);
        part.softRender.vdata[i * 4 + 0] = static_cast<float>(p.x);
        part.softRender.vdata[i * 4 + 1] = static_cast<float>(p.y);
    }

    glBindBuffer(GL_ARRAY_BUFFER, part.softRender.vbo);
    glBufferSubData(
        GL_ARRAY_BUFFER,
        0,
        static_cast<GLsizeiptr>(part.softRender.vdata.size() * sizeof(float)),
        part.softRender.vdata.data()
    );
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void GameRenderer::destroySoftRenderMesh(physics::RenderPart& part) {
    if (part.softRender.ebo) {
        glDeleteBuffers(1, &part.softRender.ebo);
        part.softRender.ebo = 0;
    }
    if (part.softRender.vbo) {
        glDeleteBuffers(1, &part.softRender.vbo);
        part.softRender.vbo = 0;
    }
    if (part.softRender.vao) {
        glDeleteVertexArrays(1, &part.softRender.vao);
        part.softRender.vao = 0;
    }

    part.softRender.indexCount = 0;
    part.softRender.vdata.clear();
}

void GameRenderer::updateQuadVBO_AxisAligned(
    float x0, float y0,
    float x1, float y1,
    float u0, float v0,
    float u1, float v1
) {
    const float vdata[16] = {
        x0, y0, u0, v0,
        x1, y0, u1, v0,
        x1, y1, u1, v1,
        x0, y1, u0, v1
    };

    glBindBuffer(GL_ARRAY_BUFFER, quadVbo_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vdata), vdata);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void GameRenderer::updateQuadVBO_Rotated(
    float cx, float cy,
    float hw, float hh,
    float angleRad,
    float u0, float v0, float u1, float v1
) {
    const float lx[4] = { -hw, +hw, +hw, -hw };
    const float ly[4] = { -hh, -hh, +hh, +hh };

    const float c = std::cos(angleRad);
    const float s = std::sin(angleRad);

    float vdata[16];
    for (int i = 0; i < 4; ++i) {
        const float rx = lx[i] * c - ly[i] * s;
        const float ry = lx[i] * s + ly[i] * c;

        const float x = cx + rx;
        const float y = cy + ry;

        const float u = (i == 0 || i == 3) ? u0 : u1;
        const float v = (i == 0 || i == 1) ? v0 : v1;

        vdata[i * 4 + 0] = x;
        vdata[i * 4 + 1] = y;
        vdata[i * 4 + 2] = u;
        vdata[i * 4 + 3] = v;
    }

    glBindBuffer(GL_ARRAY_BUFFER, quadVbo_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vdata), vdata);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void GameRenderer::renderParts(const std::vector<physics::RenderItem>& items) {
    for (const physics::RenderItem& it : items) {
        if (!it.part) {
            continue;
        }

        const physics::RenderPart& part = *it.part;
        glActiveTexture(GL_TEXTURE0);

        if (it.kind == physics::RenderItemKind::Part) {
            if (part.kind == physics::PartKind::Rigid) {
                if (!part.rigid.body || !part.render.tex) {
                    continue;
                }

                const cpVect p = cpBodyGetPosition(part.rigid.body);
                const float ang = static_cast<float>(cpBodyGetAngle(part.rigid.body));

                glBindTexture(GL_TEXTURE_2D, part.render.tex);

                updateQuadVBO_Rotated(
                    static_cast<float>(p.x),
                    static_cast<float>(p.y),
                    part.render.halfW,
                    part.render.halfH,
                    ang,
                    part.render.u0,
                    part.render.v0,
                    part.render.u1,
                    part.render.v1
                );

                glBindVertexArray(quadVao_);
                glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr);
            } else {
                if (!part.softRender.vao || !part.render.tex) {
                    continue;
                }

                glBindVertexArray(part.softRender.vao);
                glBindTexture(GL_TEXTURE_2D, part.render.tex);
                glDrawElements(
                    GL_TRIANGLES,
                    static_cast<GLsizei>(part.softRender.indexCount),
                    GL_UNSIGNED_INT,
                    nullptr
                );
            }
        } else if (it.kind == physics::RenderItemKind::SoftOverlay) {
            if (part.kind != physics::PartKind::Soft) {
                continue;
            }

            const int oi = it.overlayIndex;
            if (oi < 0 || oi >= static_cast<int>(part.overlays.size())) {
                continue;
            }

            if (!part.softRender.vao || !part.overlays[oi].tex) {
                continue;
            }

            glBindVertexArray(part.softRender.vao);
            glBindTexture(GL_TEXTURE_2D, part.overlays[oi].tex);
            glDrawElements(
                GL_TRIANGLES,
                static_cast<GLsizei>(part.softRender.indexCount),
                GL_UNSIGNED_INT,
                nullptr
            );
        }
    }
}

} // namespace render