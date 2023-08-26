#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <cglm/cglm.h>

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t i3Index0;
    uint32_t n3Indexs;
    uint8_t (*pixels)[4];
} ZigBSPTex;

typedef struct {
    uint8_t *vbo_data;
    size_t vbo_size;
    uint8_t *ebo_data;
    size_t ebo_size;
    ZigBSPTex *textures;
    size_t text_cnt;
} ZigLoadBSP;

int32_t zigLoadBSP(const char *filename, ZigLoadBSP *result);
void zigFreeBSP(ZigLoadBSP *result);

static void cbGlfwError(int error, const char *description) {
    fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

static void cbGLDebug(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar *message, const void *userParam) {
    fprintf(stderr, "GL Debug: %s\n", message);
}

typedef struct _userdata {
    double prev_xpos;
    double prev_ypos;
    bool captured;
    bool in_w;
    bool in_s;
    bool in_a;
    bool in_d;
    bool in_fast;
    vec3 pos;
    vec3 ang;
} userdata_t;

static void cbGlfwKeyFn(GLFWwindow *window, int key, int scancode, int action, int mods) {
    userdata_t *ud = (userdata_t *)glfwGetWindowUserPointer(window);
    bool pressed = action != GLFW_RELEASE;
    if (ud->captured)
        switch (key) {
        case GLFW_KEY_W:
            ud->in_w = pressed;
            break;
        case GLFW_KEY_S:
            ud->in_s = pressed;
            break;
        case GLFW_KEY_A:
            ud->in_a = pressed;
            break;
        case GLFW_KEY_D:
            ud->in_d = pressed;
            break;
        case GLFW_KEY_LEFT_SHIFT:
            ud->in_fast = pressed;
            break;
        }
    if (pressed)
        switch (key) {
        case GLFW_KEY_V:
            glm_vec3_print(ud->pos, stderr);
            glm_vec3_print(ud->ang, stderr);
            break;
        case GLFW_KEY_ESCAPE:
            ud->in_w = false;
            ud->in_s = false;
            ud->in_a = false;
            ud->in_d = false;
            ud->captured = false;
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            break;
        case GLFW_KEY_Q:
            ud->captured = false;
            glfwSetWindowShouldClose(window, GLFW_TRUE);
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            break;
        }
}

static void cbGlfwBtnFn(GLFWwindow *window, int button, int action, int mods) {
    if (action != GLFW_PRESS)
        return;
    userdata_t *ud = (userdata_t *)glfwGetWindowUserPointer(window);
    switch (button) {
    case GLFW_MOUSE_BUTTON_LEFT:
        ud->captured = true;
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        glfwGetCursorPos(window, &ud->prev_xpos, &ud->prev_ypos);
        break;
    case GLFW_MOUSE_BUTTON_RIGHT:
        ud->captured = false;
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        break;
    }
}

static void cbGlfwPosFn(GLFWwindow *window, double xpos, double ypos) {
    userdata_t *ud = (userdata_t *)glfwGetWindowUserPointer(window);
    if (!ud->captured)
        return;
    double xmove = xpos - ud->prev_xpos;
    double ymove = ypos - ud->prev_ypos;
    ud->prev_xpos = xpos;
    ud->prev_ypos = ypos;
    ud->ang[0] -= xmove * 0.022 * 4.5;
    ud->ang[1] += ymove * 0.022 * 4.5;
    if (ud->ang[0] >= 180.0)
        ud->ang[0] -= 360.0;
    if (ud->ang[0] < -180.0)
        ud->ang[0] += 360.0;
    ud->ang[1] = glm_clamp(ud->ang[1], -89.0, +89.0);
}

char *readFile(const char *filename) {
    FILE *fp = fopen(filename, "rb");
    fseek(fp, 0, SEEK_END);
    long len = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    char *data = malloc(len + 1);
    fread(data, 1, len, fp);
    data[len] = '\0';
    fclose(fp);
    return data;
}

void angle_vectors(vec3 degs, vec3 f, vec3 s, vec3 u) {
    float y = glm_rad(degs[0]);
    float p = glm_rad(degs[1]);
    float r = glm_rad(degs[2]);
    float sr = sin(r), sp = sin(p), sy = sin(y);
    float cr = cos(r), cp = cos(p), cy = cos(y);
    f[0] = cp * cy;
    f[1] = cp * sy;
    f[2] = -sp;
    s[0] = -sr * sp * cy + cr * sy;
    s[1] = -sr * sp * sy - cr * cy;
    s[2] = -sr * cp;
    u[0] = cr * sp * cy + sr * sy;
    u[1] = cr * sp * sy - sr * cy;
    u[2] = cr * cp;
}

int main(int argc, char **argv) {
    glfwSetErrorCallback(cbGlfwError);
    glfwInit();
    glfwWindowHint(GLFW_RESIZABLE, false);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    GLFWwindow *window = glfwCreateWindow(1024, 768, "GL Game", NULL, NULL);
    glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
    glfwMakeContextCurrent(window);
    glewExperimental = true;
    glewInit();
    glDebugMessageCallback(cbGLDebug, NULL);

    // fprintf(stderr, "GLEW_ARB_bindless_texture = %d\n", GLEW_ARB_bindless_texture);

    userdata_t ud;
    memset(&ud, 0, sizeof(ud));

    glfwSetWindowUserPointer(window, &ud);
    glfwSetKeyCallback(window, cbGlfwKeyFn);
    glfwSetCursorPosCallback(window, cbGlfwPosFn);
    glfwSetMouseButtonCallback(window, cbGlfwBtnFn);

    GLuint sh = glCreateProgram();
    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    char *vss = readFile("v.glsl");
    char *fss = readFile("f.glsl");
    glShaderSource(vs, 1, (const GLchar *const *)&vss, NULL);
    glShaderSource(fs, 1, (const GLchar *const *)&fss, NULL);
    free(vss);
    free(fss);
    glCompileShader(vs);
    glCompileShader(fs);
    glAttachShader(sh, vs);
    glAttachShader(sh, fs);
    glLinkProgram(sh);
    glDetachShader(sh, vs);
    glDetachShader(sh, fs);
    glDeleteShader(vs);
    glDeleteShader(fs);
    glUseProgram(sh);
    GLuint vao, vbo, ebo;
    glCreateVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glGenBuffers(1, &ebo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);

    ZigLoadBSP bsp;
    bool bspload = false;
    GLuint *texObjs = NULL;
    if (zigLoadBSP(argv[1], &bsp) == 0) {
        glBufferData(GL_ARRAY_BUFFER, bsp.vbo_size, bsp.vbo_data, GL_STATIC_DRAW);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, bsp.ebo_size, bsp.ebo_data, GL_STATIC_DRAW);
        texObjs = malloc(sizeof(GLuint) * bsp.text_cnt);
        glGenTextures(bsp.text_cnt, texObjs);
        for (uint32_t i = 0; i < bsp.text_cnt; i++) {
            ZigBSPTex bsptex = bsp.textures[i];
            glBindTexture(GL_TEXTURE_2D, texObjs[i]);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, bsptex.width, bsptex.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, bsptex.pixels);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glGenerateMipmap(GL_TEXTURE_2D);
        }
        fprintf(stderr, "loaded: vertices: %zu indices: %zu textures: %zu\n", bsp.vbo_size / sizeof(float[3]), bsp.ebo_size / sizeof(uint32_t), bsp.text_cnt);
        bspload = true;
    }

    GLuint locVtxPos = glGetAttribLocation(sh, "vtxPos");
    GLuint locTexPos = glGetAttribLocation(sh, "texPos");
    glEnableVertexAttribArray(locVtxPos);
    glEnableVertexAttribArray(locTexPos);
    glVertexAttribPointer(locVtxPos, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void *)(0 * sizeof(float)));
    glVertexAttribPointer(locTexPos, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void *)(3 * sizeof(float)));
    GLuint locMVP = glGetUniformLocation(sh, "mvp");
    GLuint locTex = glGetUniformLocation(sh, "tex");
    glUniform1i(locTex, 0); // GL_TEXTURE0

    mat4 m_proj, m_view, m_mvp;
    vec3 v_f, v_s, v_u, v_center, v_wish;
    glm_perspective(glm_rad(60.0), 4.0 / 3.0, 16.0, 16384.0, m_proj);

    glfwSwapInterval(0);
    glClearColor(0.2f, 0.3f, 0.3f, 1.0f);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    // glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

    double prevTime = glfwGetTime();
    double prevFpsX = glfwGetTime();
    int fps = 0;

    glActiveTexture(GL_TEXTURE0);

    while (glfwWindowShouldClose(window) == 0) {
        double currTime = glfwGetTime();
        double dt = currTime - prevTime;
        prevTime = currTime;
        fps++;
        if (currTime - prevFpsX >= 1.0) {
            prevFpsX = currTime;
            fprintf(stderr, "avg fps: %d\n", fps);
            fps = 0;
        }
        glfwPollEvents();
        float fmove = 0.0, smove = 0.0;
        if (ud.in_w)
            fmove += 1;
        if (ud.in_s)
            fmove -= 1;
        if (ud.in_a)
            smove -= 1;
        if (ud.in_d)
            smove += 1;
        angle_vectors(ud.ang, v_f, v_s, v_u);
        glm_vec3_zero(v_wish);
        glm_vec3_muladds(v_f, fmove, v_wish);
        glm_vec3_muladds(v_s, smove, v_wish);
        glm_vec3_normalize(v_wish);
        float speed = ud.in_fast ? 1000.0 : 250.0;
        glm_vec3_muladds(v_wish, dt * speed, ud.pos);
        glm_vec3_add(ud.pos, v_f, v_center);
        glm_lookat(ud.pos, v_center, (vec3){0, 0, 1}, m_view);
        glm_mat4_mul(m_proj, m_view, m_mvp);
        glUniformMatrix4fv(locMVP, 1, GL_FALSE, &m_mvp[0][0]);

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        for (uint32_t i = 0; i < bsp.text_cnt; i++) {
            ZigBSPTex bsptex = bsp.textures[i];
            if (bsptex.n3Indexs > 0) {
                glBindTexture(GL_TEXTURE_2D, texObjs[i]);
                glDrawElements(GL_TRIANGLES, bsptex.n3Indexs * 3, GL_UNSIGNED_INT, (void *)(sizeof(uint32_t[3]) * bsptex.i3Index0));
            }
        }

        glfwSwapBuffers(window);
    }

    if (bspload) {
        free(texObjs);
        zigFreeBSP(&bsp);
    }

    glfwTerminate();
    return 0;
}