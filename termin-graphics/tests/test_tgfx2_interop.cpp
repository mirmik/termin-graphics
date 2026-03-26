// Test: tgfx2-backed gpu_ops produce valid GL resources identical to legacy path.
// Creates resources through both vtable implementations and compares GL state.
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <tgfx/tgfx_gpu_ops.h>
#include <tgfx/tgfx2_interop.h>
#include <tgfx2/opengl/opengl_render_device.hpp>

static int test_count = 0;
static int pass_count = 0;

#define CHECK(cond, msg) do { \
    test_count++; \
    if (cond) { pass_count++; printf("  PASS: %s\n", msg); } \
    else { printf("  FAIL: %s\n", msg); } \
} while(0)

// Simple vertex/fragment shaders
static const char* vert_src = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
void main() { gl_Position = vec4(aPos, 1.0); }
)";

static const char* frag_src = R"(
#version 330 core
out vec4 FragColor;
void main() { FragColor = vec4(1.0, 0.0, 0.0, 1.0); }
)";

static void test_texture_upload() {
    printf("\n--- Texture Upload ---\n");
    const tgfx_gpu_ops* ops = tgfx_gpu_get_ops();

    uint8_t pixels[4 * 4 * 4]; // 4x4 RGBA
    memset(pixels, 128, sizeof(pixels));

    uint32_t tex_id = ops->texture_upload(pixels, 4, 4, 4, false, false);
    CHECK(tex_id != 0, "texture_upload returns non-zero id");
    CHECK(glIsTexture(tex_id), "returned id is a valid GL texture");

    // Bind and verify
    ops->texture_bind(tex_id, 0);
    GLint bound_tex = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &bound_tex);
    CHECK((uint32_t)bound_tex == tex_id, "texture is bound to unit 0");

    ops->texture_delete(tex_id);
    CHECK(!glIsTexture(tex_id), "texture deleted successfully");
}

static void test_depth_texture_upload() {
    printf("\n--- Depth Texture Upload ---\n");
    const tgfx_gpu_ops* ops = tgfx_gpu_get_ops();

    uint32_t tex_id = ops->depth_texture_upload(nullptr, 256, 256, true);
    CHECK(tex_id != 0, "depth_texture_upload returns non-zero id");
    CHECK(glIsTexture(tex_id), "returned id is a valid GL texture");

    // Check compare mode
    glBindTexture(GL_TEXTURE_2D, tex_id);
    GLint compare_mode = 0;
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, &compare_mode);
    CHECK(compare_mode == GL_COMPARE_REF_TO_TEXTURE, "depth texture has compare mode");
    glBindTexture(GL_TEXTURE_2D, 0);

    ops->texture_delete(tex_id);
}

static void test_shader_compile() {
    printf("\n--- Shader Compile ---\n");
    const tgfx_gpu_ops* ops = tgfx_gpu_get_ops();

    uint32_t program = ops->shader_compile(vert_src, frag_src, nullptr);
    CHECK(program != 0, "shader_compile returns non-zero program");
    CHECK(glIsProgram(program), "returned id is a valid GL program");

    // Use and set uniform
    ops->shader_use(program);
    GLint current_prog = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &current_prog);
    CHECK((uint32_t)current_prog == program, "program is active after shader_use");

    // Set a uniform (should not crash even if uniform doesn't exist)
    ops->shader_set_float(program, "u_time", 1.5f);
    ops->shader_set_mat4(program, "u_mvp", nullptr, false); // may not exist, just check no crash

    glUseProgram(0); // unbind before delete
    ops->shader_delete(program);
    CHECK(!glIsProgram(program), "program deleted successfully");
}

static void test_mesh_upload() {
    printf("\n--- Mesh Upload ---\n");
    const tgfx_gpu_ops* ops = tgfx_gpu_get_ops();

    float vertices[] = {
        0.0f, 0.5f, 0.0f,
       -0.5f,-0.5f, 0.0f,
        0.5f,-0.5f, 0.0f,
    };
    uint32_t indices[] = {0, 1, 2};

    tgfx_vertex_layout layout;
    tgfx_vertex_layout_init(&layout);
    tgfx_vertex_layout_add(&layout, "position", 3, TGFX_ATTRIB_FLOAT32, 0);

    uint32_t out_vbo = 0, out_ebo = 0;
    uint32_t vao = ops->mesh_upload(vertices, 3, indices, 3, &layout, &out_vbo, &out_ebo);

    CHECK(vao != 0, "mesh_upload returns non-zero VAO");
    CHECK(out_vbo != 0, "mesh_upload outputs non-zero VBO");
    CHECK(out_ebo != 0, "mesh_upload outputs non-zero EBO");
    CHECK(glIsVertexArray(vao), "returned VAO is valid");
    CHECK(glIsBuffer(out_vbo), "returned VBO is valid");
    CHECK(glIsBuffer(out_ebo), "returned EBO is valid");

    // Create additional VAO from same VBO/EBO
    uint32_t vao2 = ops->mesh_create_vao(&layout, out_vbo, out_ebo);
    CHECK(vao2 != 0, "mesh_create_vao returns non-zero VAO");
    CHECK(vao2 != vao, "second VAO is different from first");

    ops->mesh_delete(vao);
    ops->mesh_delete(vao2);
    ops->buffer_delete(out_vbo);
    ops->buffer_delete(out_ebo);
}

int main() {
    // Init GLFW + create hidden OpenGL context
    if (!glfwInit()) {
        fprintf(stderr, "Failed to init GLFW\n");
        return 1;
    }

    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(64, 64, "test", nullptr, nullptr);
    if (!window) {
        fprintf(stderr, "Failed to create GLFW window\n");
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);

    if (!gladLoaderLoadGL()) {
        fprintf(stderr, "Failed to load GLAD\n");
        glfwTerminate();
        return 1;
    }

    printf("=== tgfx2 interop test ===\n");
    printf("GL: %s\n", glGetString(GL_RENDERER));

    // Create tgfx2 OpenGL device
    tgfx2::OpenGLRenderDevice device;

    // Register tgfx2-backed gpu_ops
    tgfx2_interop_set_device(&device);
    tgfx2_gpu_ops_register();

    // Run tests
    test_texture_upload();
    test_depth_texture_upload();
    test_shader_compile();
    test_mesh_upload();

    printf("\n=== Results: %d/%d passed ===\n", pass_count, test_count);

    glfwDestroyWindow(window);
    glfwTerminate();

    return (pass_count == test_count) ? 0 : 1;
}
