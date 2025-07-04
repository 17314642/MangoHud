// dear imgui: Renderer for modern OpenGL with shaders / programmatic pipeline
// - Desktop GL: 2.x 3.x 4.x
// - Embedded GL: ES 2.0 (WebGL 1.0), ES 3.0 (WebGL 2.0)
// This needs to be used along with a Platform Binding (e.g. GLFW, SDL, Win32, custom..)

// Implemented features:
//  [X] Renderer: User texture binding. Use 'GLuint' OpenGL texture identifier as void*/ImTextureID. Read the FAQ about ImTextureID!
//  [x] Renderer: Desktop GL only: Support for large meshes (64k+ vertices) with 16-bit indices.

// You can copy and use unmodified imgui_impl_* files in your project. See main.cpp for an example of using this.
// If you are new to dear imgui, read examples/README.txt and read the documentation at the top of imgui.cpp.
// https://github.com/ocornut/imgui

// CHANGELOG
// (minor and older changes stripped away, please see git history for details)
//  2020-04-12: OpenGL: Fixed context version check mistakenly testing for 4.0+ instead of 3.2+ to enable ImGuiBackendFlags_RendererHasVtxOffset.
//  2020-01-07: OpenGL: Added support for glbindings OpenGL loader.
//  2019-10-25: OpenGL: Using a combination of GL define and runtime GL version to decide whether to use glDrawElementsBaseVertex(). Fix building with pre-3.2 GL loaders.
//  2019-09-22: OpenGL: Detect default GL loader using __has_include compiler facility.
//  2019-09-16: OpenGL: Tweak initialization code to allow application calling ImGui_ImplOpenGL3_CreateFontsTexture() before the first NewFrame() call.
//  2019-05-29: OpenGL: Desktop GL only: Added support for large mesh (64K+ vertices), enable ImGuiBackendFlags_RendererHasVtxOffset flag.
//  2019-04-30: OpenGL: Added support for special ImDrawCallback_ResetRenderState callback to reset render state.
//  2019-03-29: OpenGL: Not calling glBindBuffer more than necessary in the render loop.
//  2019-03-15: OpenGL: Added a dummy GL call + comments in ImGui_ImplOpenGL3_Init() to detect uninitialized GL function loaders early.
//  2019-03-03: OpenGL: Fix support for ES 2.0 (WebGL 1.0).
//  2019-02-20: OpenGL: Fix for OSX not supporting OpenGL 4.5, we don't try to read GL_CLIP_ORIGIN even if defined by the headers/loader.
//  2019-02-11: OpenGL: Projecting clipping rectangles correctly using draw_data->FramebufferScale to allow multi-viewports for retina display.
//  2019-02-01: OpenGL: Using GLSL 410 shaders for any version over 410 (e.g. 430, 450).
//  2018-11-30: Misc: Setting up io.BackendRendererName so it can be displayed in the About Window.
//  2018-11-13: OpenGL: Support for GL 4.5's glClipControl(GL_UPPER_LEFT) / GL_CLIP_ORIGIN.
//  2018-08-29: OpenGL: Added support for more OpenGL loaders: glew and glad, with comments indicative that any loader can be used.
//  2018-08-09: OpenGL: Default to OpenGL ES 3 on iOS and Android. GLSL version default to "#version 300 ES".
//  2018-07-30: OpenGL: Support for GLSL 300 ES and 410 core. Fixes for Emscripten compilation.
//  2018-07-10: OpenGL: Support for more GLSL versions (based on the GLSL version string). Added error output when shaders fail to compile/link.
//  2018-06-08: Misc: Extracted imgui_impl_opengl3.cpp/.h away from the old combined GLFW/SDL+OpenGL3 examples.
//  2018-06-08: OpenGL: Use draw_data->DisplayPos and draw_data->DisplaySize to setup projection matrix and clipping rectangle.
//  2018-05-25: OpenGL: Removed unnecessary backup/restore of GL_ELEMENT_ARRAY_BUFFER_BINDING since this is part of the VAO state.
//  2018-05-14: OpenGL: Making the call to glBindSampler() optional so 3.2 context won't fail if the function is a NULL pointer.
//  2018-03-06: OpenGL: Added const char* glsl_version parameter to ImGui_ImplOpenGL3_Init() so user can override the GLSL version e.g. "#version 150".
//  2018-02-23: OpenGL: Create the VAO in the render function so the setup can more easily be used with multiple shared GL context.
//  2018-02-16: Misc: Obsoleted the io.RenderDrawListsFn callback and exposed ImGui_ImplSdlGL3_RenderDrawData() in the .h file so you can call it yourself.
//  2018-01-07: OpenGL: Changed GLSL shader version from 330 to 150.
//  2017-09-01: OpenGL: Save and restore current bound sampler. Save and restore current polygon mode.
//  2017-05-01: OpenGL: Fixed save and restore of current blend func state.
//  2017-05-01: OpenGL: Fixed save and restore of current GL_ACTIVE_TEXTURE.
//  2016-09-05: OpenGL: Fixed save and restore of current scissor rectangle.
//  2016-07-29: OpenGL: Explicitly setting GL_UNPACK_ROW_LENGTH to reduce issues because SDL changes it. (#752)

//----------------------------------------
// OpenGL    GLSL      GLSL
// version   version   string
//----------------------------------------
//  2.0       110       "#version 110"
//  2.1       120       "#version 120"
//  3.0       130       "#version 130"
//  3.1       140       "#version 140"
//  3.2       150       "#version 150"
//  3.3       330       "#version 330 core"
//  4.0       400       "#version 400 core"
//  4.1       410       "#version 410 core"
//  4.2       420       "#version 410 core"
//  4.3       430       "#version 430 core"
//  ES 2.0    100       "#version 100"      = WebGL 1.0
//  ES 3.0    300       "#version 300 es"   = WebGL 2.0
//----------------------------------------

#include <spdlog/spdlog.h>
#include <imgui.h>
#include "gl_renderer.h"
#include <stdio.h>
#include <stdint.h>     // intptr_t
#include <sstream>

#include <spdlog/spdlog.h>
#include <glad/glad.h>

#include "overlay.h"
#include "../server_connection.hpp"

namespace MangoHud { namespace GL {

extern overlay_params params;

// OpenGL Data
static GLuint       g_GlVersion = 0;                // Extracted at runtime using GL_MAJOR_VERSION, GL_MINOR_VERSION queries.
static char         g_GlslVersionString[32] = "";   // Specified by user or detected based on compile time GL settings.
static GLuint       g_FontTexture = 0;
static GLuint       g_ShaderHandle = 0, g_VertHandle = 0, g_FragHandle = 0;
static int          g_AttribLocationTex = 0, g_AttribLocationProjMtx = 0;                                // Uniforms location
static int          g_AttribLocationVtxPos = 0, g_AttribLocationVtxUV = 0, g_AttribLocationVtxColor = 0; // Vertex attributes location
static unsigned int g_VboHandle = 0, g_ElementsHandle = 0;
static bool         g_IsGLES = false;

// Functions
static void ImGui_ImplOpenGL3_DestroyFontsTexture()
{
    if (g_FontTexture)
    {
        ImGuiIO& io = ImGui::GetIO();
        glDeleteTextures(1, &g_FontTexture);
        io.Fonts->SetTexID(0);
        g_FontTexture = 0;
    }
}

bool ImGui_ImplOpenGL3_CreateFontsTexture()
{
    ImGui_ImplOpenGL3_DestroyFontsTexture();
    // Build texture atlas
    ImGuiIO& io = ImGui::GetIO();
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsAlpha8(&pixels, &width, &height);

    GLint last_texture, last_unpack_buffer;

    // Upload texture to graphics system
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &last_texture);
    glGenTextures(1, &g_FontTexture);
    glBindTexture(GL_TEXTURE_2D, g_FontTexture);
    if ((g_IsGLES && g_GlVersion >= 300) || (!g_IsGLES && g_GlVersion >= 210))
    {
        glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &last_unpack_buffer);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    //#ifdef GL_UNPACK_ROW_LENGTH
    if (g_IsGLES || g_GlVersion >= 200)
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, width, height, 0, GL_RED, GL_UNSIGNED_BYTE, pixels);

    // Store our identifier
    io.Fonts->SetTexID((ImTextureID)(intptr_t)g_FontTexture);

    // Restore state
    glBindTexture(GL_TEXTURE_2D, last_texture);
    if ((g_IsGLES && g_GlVersion >= 300) || (!g_IsGLES && g_GlVersion >= 210))
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, last_unpack_buffer);

    return true;
}

// If you get an error please report on github. You may try different GL context version or GLSL version. See GL<>GLSL version table at the top of this file.
static bool CheckShader(GLuint handle, const char* desc)
{
    GLint status = 0, log_length = 0;
    glGetShaderiv(handle, GL_COMPILE_STATUS, &status);
    glGetShaderiv(handle, GL_INFO_LOG_LENGTH, &log_length);
    if ((GLboolean)status == GL_FALSE)
        SPDLOG_ERROR("ImGui_ImplOpenGL3_CreateDeviceObjects: failed to compile {}!", desc);
    if (log_length > 1)
    {
        ImVector<char> buf;
        buf.resize((int)(log_length + 1));
        glGetShaderInfoLog(handle, log_length, NULL, (GLchar*)buf.begin());
        SPDLOG_ERROR("{}", buf.begin());
    }
    return (GLboolean)status == GL_TRUE;
}

// If you get an error please report on GitHub. You may try different GL context version or GLSL version.
static bool CheckProgram(GLuint handle, const char* desc)
{
    GLint status = 0, log_length = 0;
    glGetProgramiv(handle, GL_LINK_STATUS, &status);
    glGetProgramiv(handle, GL_INFO_LOG_LENGTH, &log_length);
    if ((GLboolean)status == GL_FALSE)
        SPDLOG_ERROR("ImGui_ImplOpenGL3_CreateDeviceObjects: failed to link {}! (with GLSL '{}')", desc, g_GlslVersionString);
    if (log_length > 1)
    {
        ImVector<char> buf;
        buf.resize((int)(log_length + 1));
        glGetProgramInfoLog(handle, log_length, NULL, (GLchar*)buf.begin());
        SPDLOG_ERROR("{}", buf.begin());
    }
    return (GLboolean)status == GL_TRUE;
}

static bool    ImGui_ImplOpenGL3_CreateDeviceObjects()
{
    // Backup GL state
    GLint last_texture, last_array_buffer;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &last_texture);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &last_array_buffer);
    //#ifndef IMGUI_IMPL_OPENGL_ES2
    GLint last_vertex_array;
    if (g_GlVersion >= 300)
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &last_vertex_array);

    // Parse GLSL version string
    int glsl_version = 120;
    sscanf(g_GlslVersionString, "#version %d", &glsl_version);

    const GLchar* vertex_shader_glsl_120 =
        "uniform mat4 ProjMtx;\n"
        "attribute vec2 Position;\n"
        "attribute vec2 UV;\n"
        "attribute vec4 Color;\n"
        "varying vec2 Frag_UV;\n"
        "varying vec4 Frag_Color;\n"
        "void main()\n"
        "{\n"
        "    Frag_UV = UV;\n"
        "    Frag_Color = Color;\n"
        "    gl_Position = ProjMtx * vec4(Position.xy,0,1);\n"
        "}\n";

    const GLchar* vertex_shader_glsl_130 =
        "uniform mat4 ProjMtx;\n"
        "in vec2 Position;\n"
        "in vec2 UV;\n"
        "in vec4 Color;\n"
        "out vec2 Frag_UV;\n"
        "out vec4 Frag_Color;\n"
        "void main()\n"
        "{\n"
        "    Frag_UV = UV;\n"
        "    Frag_Color = Color;\n"
        "    gl_Position = ProjMtx * vec4(Position.xy,0,1);\n"
        "}\n";

    const GLchar* vertex_shader_glsl_300_es =
        "precision mediump float;\n"
        "layout (location = 0) in vec2 Position;\n"
        "layout (location = 1) in vec2 UV;\n"
        "layout (location = 2) in vec4 Color;\n"
        "uniform mat4 ProjMtx;\n"
        "out vec2 Frag_UV;\n"
        "out vec4 Frag_Color;\n"
        "void main()\n"
        "{\n"
        "    Frag_UV = UV;\n"
        "    Frag_Color = Color;\n"
        "    gl_Position = ProjMtx * vec4(Position.xy,0,1);\n"
        "}\n";

    const GLchar* vertex_shader_glsl_410_core =
        "layout (location = 0) in vec2 Position;\n"
        "layout (location = 1) in vec2 UV;\n"
        "layout (location = 2) in vec4 Color;\n"
        "uniform mat4 ProjMtx;\n"
        "out vec2 Frag_UV;\n"
        "out vec4 Frag_Color;\n"
        "void main()\n"
        "{\n"
        "    Frag_UV = UV;\n"
        "    Frag_Color = Color;\n"
        "    gl_Position = ProjMtx * vec4(Position.xy,0,1);\n"
        //"    gl_Position = ProjMtx * vec4(Position.xy,0,1) * vec4(1.0, -1.0, 1, 1);\n"
        "}\n";

    const GLchar* fragment_shader_glsl_120 =
        "#ifdef GL_ES\n"
        "    precision mediump float;\n"
        "#endif\n"
        "uniform sampler2D Texture;\n"
        "varying vec2 Frag_UV;\n"
        "varying vec4 Frag_Color;\n"
        "void main()\n"
        "{\n"
        "    gl_FragColor = Frag_Color * vec4(1, 1, 1, texture2D(Texture, Frag_UV.st).r);\n"
        "}\n";

    const GLchar* fragment_shader_glsl_130 =
        "uniform sampler2D Texture;\n"
        "in vec2 Frag_UV;\n"
        "in vec4 Frag_Color;\n"
        "out vec4 Out_Color;\n"
        "void main()\n"
        "{\n"
        "    Out_Color = Frag_Color * vec4(1, 1, 1, texture(Texture, Frag_UV.st).r);\n"
        "}\n";

    const GLchar* fragment_shader_glsl_300_es =
        "precision mediump float;\n"
        "uniform sampler2D Texture;\n"
        "in vec2 Frag_UV;\n"
        "in vec4 Frag_Color;\n"
        "layout (location = 0) out vec4 Out_Color;\n"
        "void main()\n"
        "{\n"
        "    Out_Color = Frag_Color * vec4(1, 1, 1, texture(Texture, Frag_UV.st).r);\n"
        "}\n";

    const GLchar* fragment_shader_glsl_410_core =
        "in vec2 Frag_UV;\n"
        "in vec4 Frag_Color;\n"
        "uniform sampler2D Texture;\n"
        "layout (location = 0) out vec4 Out_Color;\n"
        "void main()\n"
        "{\n"
        "    Out_Color = Frag_Color * vec4(1, 1, 1, texture(Texture, Frag_UV.st).r);\n"
        "}\n";

    SPDLOG_DEBUG("glsl_version: {}", glsl_version);

    // Select shaders matching our GLSL versions
    const GLchar* vertex_shader = NULL;
    const GLchar* fragment_shader = NULL;
    if (glsl_version < 130)
    {
        vertex_shader = vertex_shader_glsl_120;
        fragment_shader = fragment_shader_glsl_120;
    }
    else if (glsl_version >= 410)
    {
        vertex_shader = vertex_shader_glsl_410_core;
        fragment_shader = fragment_shader_glsl_410_core;
    }
    else if (glsl_version == 300)
    {
        vertex_shader = vertex_shader_glsl_300_es;
        fragment_shader = fragment_shader_glsl_300_es;
    }
    else
    {
        vertex_shader = vertex_shader_glsl_130;
        fragment_shader = fragment_shader_glsl_130;
    }

    std::stringstream ss;
    ss << g_GlslVersionString << vertex_shader;
    std::string shader = ss.str();

    // Create shaders
    //const GLchar* vertex_shader_with_version[2] = { g_GlslVersionString, vertex_shader };
    const GLchar* vertex_shader_with_version[1] = { shader.c_str() };
    g_VertHandle = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(g_VertHandle, 1, vertex_shader_with_version, NULL);
    glCompileShader(g_VertHandle);
    CheckShader(g_VertHandle, "vertex shader");

    ss.str(""); ss.clear();
    ss << g_GlslVersionString << fragment_shader;
    shader = ss.str();

    const GLchar* fragment_shader_with_version[1] = { shader.c_str() };
    g_FragHandle = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(g_FragHandle, 1, fragment_shader_with_version, NULL);
    glCompileShader(g_FragHandle);
    CheckShader(g_FragHandle, "fragment shader");

    g_ShaderHandle = glCreateProgram();
    glAttachShader(g_ShaderHandle, g_VertHandle);
    glAttachShader(g_ShaderHandle, g_FragHandle);
    glLinkProgram(g_ShaderHandle);
    CheckProgram(g_ShaderHandle, "shader program");

    g_AttribLocationTex = glGetUniformLocation(g_ShaderHandle, "Texture");
    g_AttribLocationProjMtx = glGetUniformLocation(g_ShaderHandle, "ProjMtx");
    g_AttribLocationVtxPos = glGetAttribLocation(g_ShaderHandle, "Position");
    g_AttribLocationVtxUV = glGetAttribLocation(g_ShaderHandle, "UV");
    g_AttribLocationVtxColor = glGetAttribLocation(g_ShaderHandle, "Color");

    // Create buffers
    glGenBuffers(1, &g_VboHandle);
    glGenBuffers(1, &g_ElementsHandle);

    ImGui_ImplOpenGL3_CreateFontsTexture();

    // Restore modified GL state
    glBindTexture(GL_TEXTURE_2D, last_texture);
    glBindBuffer(GL_ARRAY_BUFFER, last_array_buffer);
    //#ifndef IMGUI_IMPL_OPENGL_ES2
    if (g_GlVersion >= 300)
        glBindVertexArray(last_vertex_array);

    return true;
}

static void    ImGui_ImplOpenGL3_DestroyDeviceObjects()
{
    if (g_VboHandle)        { glDeleteBuffers(1, &g_VboHandle); g_VboHandle = 0; }
    if (g_ElementsHandle)   { glDeleteBuffers(1, &g_ElementsHandle); g_ElementsHandle = 0; }
    if (g_ShaderHandle && g_VertHandle) { glDetachShader(g_ShaderHandle, g_VertHandle); }
    if (g_ShaderHandle && g_FragHandle) { glDetachShader(g_ShaderHandle, g_FragHandle); }
    if (g_VertHandle)       { glDeleteShader(g_VertHandle); g_VertHandle = 0; }
    if (g_FragHandle)       { glDeleteShader(g_FragHandle); g_FragHandle = 0; }
    if (g_ShaderHandle)     { glDeleteProgram(g_ShaderHandle); g_ShaderHandle = 0; }

    ImGui_ImplOpenGL3_DestroyFontsTexture();
}

void GetOpenGLVersion(int& major, int& minor, bool& isGLES)
{
    //glGetIntegerv(GL_MAJOR_VERSION, &major);
    //glGetIntegerv(GL_MINOR_VERSION, &minor);

    const char* version;
    const char* prefixes[] = {
        "OpenGL ES-CM ",
        "OpenGL ES-CL ",
        "OpenGL ES ",
        nullptr
    };

    version = (const char*) glGetString(GL_VERSION);
    if (!version)
        return;

    //if (glGetError() == 0x500)
    {
        for (int i = 0;  prefixes[i];  i++) {
            const size_t length = strlen(prefixes[i]);
            if (strncmp(version, prefixes[i], length) == 0) {
                version += length;
                isGLES = true;
                break;
            }
        }

        sscanf(version, "%d.%d", &major, &minor);
    }
}

bool    ImGui_ImplOpenGL3_Init(const char* glsl_version)
{
    GLint major = 0, minor = 0;
    GetOpenGLVersion(major, minor, g_IsGLES);

    SPDLOG_DEBUG("GL version: {}.{} {}", major, minor, g_IsGLES ? "ES" : "");

    if (!g_IsGLES) {
        // Not GL ES
        glsl_version = "#version 120";
        g_GlVersion = major * 100 + minor * 10;
        if (major >= 4 && minor >= 1)
            glsl_version = "#version 410";
        else if (major > 3 || (major == 3 && minor >= 2))
            glsl_version = "#version 150";
        else if (major == 3)
            glsl_version = "#version 130";
        else if (major < 2)
            glsl_version = "#version 100";
    } else {
        if (major >= 3)
            g_GlVersion = major * 100 + minor * 10; // GLES >= 3
        else
            g_GlVersion = 200; // GLES 2

        // Store GLSL version string so we can refer to it later in case we recreate shaders.
        // Note: GLSL version is NOT the same as GL version. Leave this to NULL if unsure.
        if (g_GlVersion == 200)
            glsl_version = "#version 100";
        else if (g_GlVersion >= 300)
            glsl_version = "#version 300 es";
        else
            glsl_version = "#version 120";
    }

    // Setup back-end capabilities flags
    ImGuiIO& io = ImGui::GetIO();
    io.BackendRendererName = "mangohud_opengl3";
    //#if IMGUI_IMPL_OPENGL_MAY_HAVE_VTX_OFFSET
    if (g_GlVersion >= 320) // GL/GLES 3.2+
        io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;  // We can honor the ImDrawCmd::VtxOffset field, allowing for large meshes.

    // Store GLSL version string so we can refer to it later in case we recreate shaders.
    // Note: GLSL version is NOT the same as GL version. Leave this to NULL if unsure.
    if (glsl_version == NULL)
        glsl_version = "#version 120";

    IM_ASSERT((int)strlen(glsl_version) + 2 < IM_ARRAYSIZE(g_GlslVersionString));
    strcpy(g_GlslVersionString, glsl_version);
    strcat(g_GlslVersionString, "\n");

    // Make a dummy GL call (we don't actually need the result)
    // IF YOU GET A CRASH HERE: it probably means that you haven't initialized the OpenGL function loader used by this code.
    // Desktop OpenGL 3/4 need a function loader. See the IMGUI_IMPL_OPENGL_LOADER_xxx explanation above.
    GLint current_texture;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &current_texture);

    setup_connection_to_server();

    return true;
}

void    ImGui_ImplOpenGL3_Shutdown()
{
    ImGui_ImplOpenGL3_DestroyDeviceObjects();
}

void    ImGui_ImplOpenGL3_NewFrame()
{
    if (!g_ShaderHandle)
        ImGui_ImplOpenGL3_CreateDeviceObjects();
    else if (!glIsProgram(g_ShaderHandle)) { // TODO Got created in a now dead context?
        SPDLOG_DEBUG("Recreating lost objects");
        ImGui_ImplOpenGL3_CreateDeviceObjects();
    }

    if (!glIsTexture(g_FontTexture)) {
        SPDLOG_DEBUG("GL Texture lost? Regenerating.");
        g_FontTexture = 0;
        ImGui_ImplOpenGL3_CreateFontsTexture();
    }
}

static void ImGui_ImplOpenGL3_SetupRenderState(ImDrawData* draw_data, int fb_width, int fb_height, GLuint vertex_array_object)
{
    // Setup render state: alpha-blending enabled, no face culling, no depth testing, scissor enabled, polygon fill
    if (params.gl_bind_framebuffer >= 0 && (g_IsGLES || g_GlVersion >= 300))
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, params.gl_bind_framebuffer);
    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glEnable(GL_SCISSOR_TEST);
    glDisable(GL_FRAMEBUFFER_SRGB);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

    if (!g_IsGLES)
    {
        //#ifdef GL_POLYGON_MODE
        if (g_GlVersion >= 200)
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        if (g_GlVersion >= 310)
            glDisable(GL_PRIMITIVE_RESTART);
    }

    bool clip_origin_lower_left = true;
    GLenum last_clip_origin = 0;
    if (!g_IsGLES && /*g_GlVersion >= 450*/ (glad_glClipControl || glad_glClipControlEXT)) {
        glGetIntegerv(GL_CLIP_ORIGIN, (GLint*)&last_clip_origin); // Support for GL 4.5's glClipControl(GL_UPPER_LEFT)
        if (last_clip_origin == GL_UPPER_LEFT)
            clip_origin_lower_left = false;
    }

    // Setup viewport, orthographic projection matrix
    // Our visible imgui space lies from draw_data->DisplayPos (top left) to draw_data->DisplayPos+data_data->DisplaySize (bottom right). DisplayPos is (0,0) for single viewport apps.
    glViewport(0, 0, (GLsizei)fb_width, (GLsizei)fb_height);
    float L = draw_data->DisplayPos.x;
    float R = draw_data->DisplayPos.x + draw_data->DisplaySize.x;
    float T = draw_data->DisplayPos.y;
    float B = draw_data->DisplayPos.y + draw_data->DisplaySize.y;
    if (!params.gl_dont_flip && !clip_origin_lower_left) { float tmp = T; T = B; B = tmp; } // Swap top and bottom if origin is upper left
    const float ortho_projection[4][4] =
    {
        { 2.0f/(R-L),   0.0f,         0.0f,   0.0f },
        { 0.0f,         2.0f/(T-B),   0.0f,   0.0f },
        { 0.0f,         0.0f,        -1.0f,   0.0f },
        { (R+L)/(L-R),  (T+B)/(B-T),  0.0f,   1.0f },
    };
    glUseProgram(g_ShaderHandle);
    glUniform1i(g_AttribLocationTex, 0);
    glUniformMatrix4fv(g_AttribLocationProjMtx, 1, GL_FALSE, &ortho_projection[0][0]);

    if (g_GlVersion >= 330)
        glBindSampler(0, 0); // We use combined texture/sampler state. Applications using GL 3.3 may set that otherwise.

    (void)vertex_array_object;
    //#ifndef IMGUI_IMPL_OPENGL_ES2
    if (g_GlVersion >= 300)
        glBindVertexArray(vertex_array_object);

    // Bind vertex/index buffers and setup attributes for ImDrawVert
    glBindBuffer(GL_ARRAY_BUFFER, g_VboHandle);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_ElementsHandle);
    glEnableVertexAttribArray(g_AttribLocationVtxPos);
    glEnableVertexAttribArray(g_AttribLocationVtxUV);
    glEnableVertexAttribArray(g_AttribLocationVtxColor);
    glVertexAttribPointer(g_AttribLocationVtxPos,   2, GL_FLOAT,         GL_FALSE, sizeof(ImDrawVert), (GLvoid*)IM_OFFSETOF(ImDrawVert, pos));
    glVertexAttribPointer(g_AttribLocationVtxUV,    2, GL_FLOAT,         GL_FALSE, sizeof(ImDrawVert), (GLvoid*)IM_OFFSETOF(ImDrawVert, uv));
    glVertexAttribPointer(g_AttribLocationVtxColor, 4, GL_UNSIGNED_BYTE, GL_TRUE,  sizeof(ImDrawVert), (GLvoid*)IM_OFFSETOF(ImDrawVert, col));
}

// OpenGL3 Render function.
// (this used to be set in io.RenderDrawListsFn and called by ImGui::Render(), but you can now call this directly from your main loop)
// Note that this implementation is little overcomplicated because we are saving/setting up/restoring every OpenGL state explicitly, in order to be able to run within any OpenGL engine that doesn't do so.
void    ImGui_ImplOpenGL3_RenderDrawData(ImDrawData* draw_data)
{
    // Avoid rendering when minimized, scale coordinates for retina displays (screen coordinates != framebuffer coordinates)
    int fb_width = (int)(draw_data->DisplaySize.x * draw_data->FramebufferScale.x);
    int fb_height = (int)(draw_data->DisplaySize.y * draw_data->FramebufferScale.y);
    if (fb_width <= 0 || fb_height <= 0 || draw_data->TotalVtxCount == 0)
        return;

    // Backup GL state
    GLint last_fb = -1;
    if (params.gl_bind_framebuffer >= 0 && (g_IsGLES || g_GlVersion >= 300))
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &last_fb);
    GLenum last_active_texture; glGetIntegerv(GL_ACTIVE_TEXTURE, (GLint*)&last_active_texture);
    glActiveTexture(GL_TEXTURE0);
    GLint last_program; glGetIntegerv(GL_CURRENT_PROGRAM, &last_program);
    GLint last_texture; glGetIntegerv(GL_TEXTURE_BINDING_2D, &last_texture);

    // GL_SAMPLER_BINDING
    GLint last_sampler;
    if (!g_IsGLES && g_GlVersion >= 330)
        glGetIntegerv(GL_SAMPLER_BINDING, &last_sampler);

    GLint last_array_buffer; glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &last_array_buffer);

//#ifndef IMGUI_IMPL_OPENGL_ES2
    GLint last_vertex_array_object;
    if (g_GlVersion >= 300)
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &last_vertex_array_object);

    GLint last_polygon_mode[2];
    if (!g_IsGLES && g_GlVersion >= 200)
        glGetIntegerv(GL_POLYGON_MODE, last_polygon_mode);

    GLint last_viewport[4]; glGetIntegerv(GL_VIEWPORT, last_viewport);
    GLint last_scissor_box[4]; glGetIntegerv(GL_SCISSOR_BOX, last_scissor_box);
    GLenum last_blend_src_rgb; glGetIntegerv(GL_BLEND_SRC_RGB, (GLint*)&last_blend_src_rgb);
    GLenum last_blend_dst_rgb; glGetIntegerv(GL_BLEND_DST_RGB, (GLint*)&last_blend_dst_rgb);
    GLenum last_blend_src_alpha; glGetIntegerv(GL_BLEND_SRC_ALPHA, (GLint*)&last_blend_src_alpha);
    GLenum last_blend_dst_alpha; glGetIntegerv(GL_BLEND_DST_ALPHA, (GLint*)&last_blend_dst_alpha);
    GLenum last_blend_equation_rgb; glGetIntegerv(GL_BLEND_EQUATION_RGB, (GLint*)&last_blend_equation_rgb);
    GLenum last_blend_equation_alpha; glGetIntegerv(GL_BLEND_EQUATION_ALPHA, (GLint*)&last_blend_equation_alpha);
    GLboolean last_enable_blend = glIsEnabled(GL_BLEND);
    GLboolean last_enable_cull_face = glIsEnabled(GL_CULL_FACE);
    GLboolean last_enable_depth_test = glIsEnabled(GL_DEPTH_TEST);
    GLboolean last_enable_stencil_test = glIsEnabled(GL_STENCIL_TEST);
    GLboolean last_enable_scissor_test = glIsEnabled(GL_SCISSOR_TEST);
    // Disable and store SRGB state.
    GLboolean last_srgb_enabled = glIsEnabled(GL_FRAMEBUFFER_SRGB);
    GLboolean last_enable_primitive_restart = (!g_IsGLES && g_GlVersion >= 310) ? glIsEnabled(GL_PRIMITIVE_RESTART) : GL_FALSE;

    // Setup desired GL state
    // Recreate the VAO every time (this is to easily allow multiple GL contexts to be rendered to. VAO are not shared among GL contexts)
    // The renderer would actually work without any VAO bound, but then our VertexAttrib calls would overwrite the default one currently bound.
    GLuint vertex_array_object = 0;
    if (g_GlVersion >= 300)
        glGenVertexArrays(1, &vertex_array_object);

    ImGui_ImplOpenGL3_SetupRenderState(draw_data, fb_width, fb_height, vertex_array_object);

    // Will project scissor/clipping rectangles into framebuffer space
    ImVec2 clip_off = draw_data->DisplayPos;         // (0,0) unless using multi-viewports
    ImVec2 clip_scale = draw_data->FramebufferScale; // (1,1) unless using retina display which are often (2,2)

    // Render command lists
    for (int n = 0; n < draw_data->CmdListsCount; n++)
    {
        const ImDrawList* cmd_list = draw_data->CmdLists[n];

        // Upload vertex/index buffers
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)cmd_list->VtxBuffer.Size * sizeof(ImDrawVert), (const GLvoid*)cmd_list->VtxBuffer.Data, GL_STREAM_DRAW);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx), (const GLvoid*)cmd_list->IdxBuffer.Data, GL_STREAM_DRAW);

        for (int cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; cmd_i++)
        {
            const ImDrawCmd* pcmd = &cmd_list->CmdBuffer[cmd_i];
            if (pcmd->UserCallback != NULL)
            {
                // User callback, registered via ImDrawList::AddCallback()
                // (ImDrawCallback_ResetRenderState is a special callback value used by the user to request the renderer to reset render state.)
                if (pcmd->UserCallback == ImDrawCallback_ResetRenderState)
                    ImGui_ImplOpenGL3_SetupRenderState(draw_data, fb_width, fb_height, vertex_array_object);
                else
                    pcmd->UserCallback(cmd_list, pcmd);
            }
            else
            {
                // Project scissor/clipping rectangles into framebuffer space
                ImVec4 clip_rect;
                clip_rect.x = (pcmd->ClipRect.x - clip_off.x) * clip_scale.x;
                clip_rect.y = (pcmd->ClipRect.y - clip_off.y) * clip_scale.y;
                clip_rect.z = (pcmd->ClipRect.z - clip_off.x) * clip_scale.x;
                clip_rect.w = (pcmd->ClipRect.w - clip_off.y) * clip_scale.y;

                if (clip_rect.x < fb_width && clip_rect.y < fb_height && clip_rect.z >= 0.0f && clip_rect.w >= 0.0f)
                {
                    // Apply scissor/clipping rectangle
                    if (!params.gl_dont_flip)
                        glScissor((int)clip_rect.x, (int)(fb_height - clip_rect.w), (int)(clip_rect.z - clip_rect.x), (int)(clip_rect.w - clip_rect.y));
                    else
                        glScissor((int)clip_rect.x, (int)clip_rect.y, (int)clip_rect.z, (int)clip_rect.w);

                    // Bind texture, Draw
                    glBindTexture(GL_TEXTURE_2D, (GLuint)(intptr_t)pcmd->TextureId);
                    //#if IMGUI_IMPL_OPENGL_MAY_HAVE_VTX_OFFSET
                    if (g_GlVersion >= 320) // OGL and OGL ES
                        glDrawElementsBaseVertex(GL_TRIANGLES, (GLsizei)pcmd->ElemCount, sizeof(ImDrawIdx) == 2 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT, (void*)(intptr_t)(pcmd->IdxOffset * sizeof(ImDrawIdx)), (GLint)pcmd->VtxOffset);
                    else
                        glDrawElements(GL_TRIANGLES, (GLsizei)pcmd->ElemCount, sizeof(ImDrawIdx) == 2 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT, (void*)(intptr_t)(pcmd->IdxOffset * sizeof(ImDrawIdx)));
                }
            }
        }
    }

    // Destroy the temporary VAO
    if (g_GlVersion >= 300)
        glDeleteVertexArrays(1, &vertex_array_object);

    // Restore modified GL state
    glUseProgram(last_program);
    glBindTexture(GL_TEXTURE_2D, last_texture);

    if (!g_IsGLES && g_GlVersion >= 330)
        glBindSampler(0, last_sampler);

    glActiveTexture(last_active_texture);

    if (g_GlVersion >= 300)
        glBindVertexArray(last_vertex_array_object);

    glBindBuffer(GL_ARRAY_BUFFER, last_array_buffer);
    glBlendEquationSeparate(last_blend_equation_rgb, last_blend_equation_alpha);
    glBlendFuncSeparate(last_blend_src_rgb, last_blend_dst_rgb, last_blend_src_alpha, last_blend_dst_alpha);
    if (last_enable_blend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    if (last_enable_cull_face) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
    if (last_enable_depth_test) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (last_enable_stencil_test) glEnable(GL_STENCIL_TEST); else glDisable(GL_STENCIL_TEST);
    if (last_enable_scissor_test) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
    if (!g_IsGLES && g_GlVersion >= 310) { if (last_enable_primitive_restart) glEnable(GL_PRIMITIVE_RESTART); }

    if (!g_IsGLES && g_GlVersion >= 200)
        glPolygonMode(GL_FRONT_AND_BACK, (GLenum)last_polygon_mode[0]);

    glViewport(last_viewport[0], last_viewport[1], (GLsizei)last_viewport[2], (GLsizei)last_viewport[3]);
    glScissor(last_scissor_box[0], last_scissor_box[1], (GLsizei)last_scissor_box[2], (GLsizei)last_scissor_box[3]);

    if (last_srgb_enabled)
        glEnable(GL_FRAMEBUFFER_SRGB);
    if (last_fb >= 0)
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, last_fb);
}

}} // namespace
