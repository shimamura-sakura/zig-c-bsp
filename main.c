#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#define CGLM_DEFINE_PRINTS
#include <cglm/cglm.h>

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t i3Index0;
    uint32_t n3Indexs;
    uint8_t (*pixels)[4];
    uint8_t skipped;
} ZigBSPTex;

typedef struct {
    vec3 n;
    float d;
    int32_t type;
} plane_t;

typedef struct {
    uint32_t iPlane;
    int16_t iChilds[2];
} clipnode_t;

typedef struct {
    uint8_t *vbo_data;
    size_t vbo_size;
    uint8_t *ebo_data;
    size_t ebo_size;
    ZigBSPTex *textures;
    size_t text_cnt;
    clipnode_t *clipnode;
    size_t clip_cnt;
    int32_t hull[3];
    plane_t *planes;
    size_t planecnt;
} ZigLoadBSP;

int32_t traverseBSP(ZigLoadBSP *bsp, int32_t node, float pos[3], float *out_normal) {
    if (node < 0) {
        if (out_normal)
            glm_vec3_zero(out_normal);
        return node;
    }
    while (true) {
        clipnode_t c = bsp->clipnode[node];
        plane_t p = bsp->planes[c.iPlane];
        bool side = glm_vec3_dot(p.n, pos) - p.d > 0.0;
        if (side)
            node = c.iChilds[0];
        else
            node = c.iChilds[1];
        if (node < 0) {
            if (out_normal) {
                glm_vec3_copy(p.n, out_normal);
                if (!side)
                    glm_vec3_negate(out_normal);
            }
            return node;
        }
    }
}

// copied from Xash3D

typedef struct {
    bool allsolid;
    bool startsolid;
    bool inopen, inwater;
    float fraction;
    vec3 endpos;
    plane_t plane;
    int ent;
    vec3 deltavelocity;
    int hitgroup;
} pmtrace_t;

// #define DIST_EPS (1.0f / 32.0f)
#define DIST_EPSILON FLT_EPSILON
#define CONTENTS_EMPTY -1
#define CONTENTS_SOLID -2

int32_t PM_HullPointContents(ZigLoadBSP *bsp, int32_t num, vec3 pos) {
    while (num >= 0) {
        clipnode_t c = bsp->clipnode[num];
        plane_t p = bsp->planes[c.iPlane];
        num = c.iChilds[glm_vec3_dot(p.n, pos) - p.d < 0];
    }
    return num;
}
bool PM_RecursiveHullCheck(ZigLoadBSP *hull, int root, int num, float p1f, float p2f, vec3 p1, vec3 p2, pmtrace_t *trace) {
    clipnode_t *node;
    plane_t *plane;
    float t1, t2;
    float frac, midf;
    int side;
    vec3 mid;
loc0:
    // check for empty
    if (num < 0) {
        if (num != CONTENTS_SOLID) {
            trace->allsolid = false;
            if (num == CONTENTS_EMPTY)
                trace->inopen = true;
            else
                trace->inwater = true;
        } else
            trace->startsolid = true;
        return true; // empty
    }

    // find the point distances
    node = hull->clipnode + num;
    plane = hull->planes + node->iPlane;

    t1 = glm_vec3_dot(p1, plane->n) - plane->d; // PlaneDiff(p1, plane);
    t2 = glm_vec3_dot(p2, plane->n) - plane->d; // PlaneDiff(p2, plane);

    if (t1 >= 0.0f && t2 >= 0.0f) {
        num = node->iChilds[0];
        goto loc0;
    }

    if (t1 < 0.0f && t2 < 0.0f) {
        num = node->iChilds[1];
        goto loc0;
    }

    // put the crosspoint DIST_EPSILON pixels on the near side
    side = (t1 < 0.0f);

    if (side)
        frac = (t1 + DIST_EPSILON) / (t1 - t2);
    else
        frac = (t1 - DIST_EPSILON) / (t1 - t2);

    if (frac < 0.0f)
        frac = 0.0f;
    if (frac > 1.0f)
        frac = 1.0f;

    midf = p1f + (p2f - p1f) * frac;
    glm_vec3_lerp(p1, p2, frac, mid); // VectorLerp(p1, frac, p2, mid);

    // move up to the node
    if (!PM_RecursiveHullCheck(hull, root, node->iChilds[side], p1f, midf, p1, mid, trace))
        return false;

    // this recursion can not be optimized because mid would need to be duplicated on a stack
    if (PM_HullPointContents(hull, node->iChilds[side ^ 1], mid) != CONTENTS_SOLID) {
        // go past the node
        return PM_RecursiveHullCheck(hull, root, node->iChilds[side ^ 1], midf, p2f, mid, p2, trace);
    }

    // never got out of the solid area
    if (trace->allsolid)
        return false;

    // the other side of the node is solid, this is the impact point
    if (!side) {
        glm_vec3_copy(plane->n, trace->plane.n);
        trace->plane.d = plane->d;
    } else {
        glm_vec3_copy(plane->n, trace->plane.n);
        glm_vec3_negate(trace->plane.n);
        trace->plane.d = -plane->d;
    }

    while (PM_HullPointContents(hull, root, mid) == CONTENTS_SOLID) {
        // shouldn't really happen, but does occasionally
        frac -= 0.1f;

        if (frac < 0.0f) {
            trace->fraction = midf;
            glm_vec3_copy(mid, trace->endpos);
            // fprintf(stderr, "trace backed up past 0.0\n");
            return false;
        }

        midf = p1f + (p2f - p1f) * frac;
        glm_vec3_lerp(p1, p2, frac, mid); // VectorLerp(p1, frac, p2, mid);
    }

    trace->fraction = midf;
    glm_vec3_copy(mid, trace->endpos);

    return false;
}

int32_t zigLoadBSP(const char *filename, ZigLoadBSP *result);
void zigFreeBSP(ZigLoadBSP *result);

static void cbGlfwError(int error, const char *description) {
    fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

static void cbGLDebug(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar *message, const void *userParam) {
    fprintf(stderr, "GL Debug: %s\n", message);
}

typedef struct _userdata {
    ZigLoadBSP *bsp;
    bool captured;
    double prev_xpos;
    double prev_ypos;
    vec3 pos;
    vec3 ang;
    vec3 vel;
    bool w;      // forward
    bool s;      // back
    bool a;      // moveleft
    bool d;      // moveright
    bool c;      // duck
    bool j;      // jump
    bool su;     // scroll up
    bool sd;     // scroll dn
    bool bPrevJ; // pressed jump
    bool bGround;
    bool bDucked; // ducked
    bool bInDuck; // duck transition
    bool bNoclip;
    float flDuckAmount;
    vec3 jumpOff;
    int hull;
} userdata_t;

static void setCapture(GLFWwindow *window, userdata_t *ud, bool capture) {
    if (ud->captured == capture)
        return;
    if ((ud->captured = capture)) {
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        glfwGetCursorPos(window, &ud->prev_xpos, &ud->prev_ypos);
    } else
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    ud->w = false;
    ud->s = false;
    ud->a = false;
    ud->d = false;
    ud->c = false;
    ud->j = false;
    ud->su = false;
    ud->sd = false;
}

static void cbGLFWKey(GLFWwindow *window, int key, int scancode, int action, int mods) {
    if (action == GLFW_REPEAT)
        return;
    userdata_t *ud = (userdata_t *)glfwGetWindowUserPointer(window);
    bool pressed = action == GLFW_PRESS;
    switch (key) {
    case GLFW_KEY_P:
        if (pressed) {
            fprintf(stderr, "pos:\n");
            glm_vec3_print(ud->pos, stderr);
        }
        break;
    case GLFW_KEY_V:
        ud->bNoclip = pressed && ud->captured;
        break;
    case GLFW_KEY_W:
        ud->w = pressed && ud->captured;
        break;
    case GLFW_KEY_S:
        ud->s = pressed && ud->captured;
        break;
    case GLFW_KEY_A:
        if ((ud->a = pressed && ud->captured))
            ud->d = false;
        break;
    case GLFW_KEY_D:
        if ((ud->d = pressed && ud->captured))
            ud->a = false;
        break;
    case GLFW_KEY_LEFT_SHIFT:
        ud->c = pressed && ud->captured;
        break;
    case GLFW_KEY_SPACE:
        ud->j = pressed && ud->captured;
        ud->w = false;
        break;
    case GLFW_KEY_ESCAPE:
        setCapture(window, ud, false);
        break;
    case GLFW_KEY_Q:
        setCapture(window, ud, true);
        glfwSetWindowShouldClose(window, GLFW_TRUE);
        break;
    }
}

static void cbGLFWScr(GLFWwindow *window, double xoffset, double yoffset) {
    userdata_t *ud = (userdata_t *)glfwGetWindowUserPointer(window);
    if (ud->captured) {
        if (yoffset > 0)
            ud->su = true;
        else
            ud->sd = true;
    }
}

static void cbGLFWPos(GLFWwindow *window, double xpos, double ypos) {
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

static void cbGLFWBtn(GLFWwindow *window, int button, int action, int mods) {
    if (action != GLFW_PRESS)
        return;
    userdata_t *ud = (userdata_t *)glfwGetWindowUserPointer(window);
    switch (button) {
    case GLFW_MOUSE_BUTTON_LEFT:
        setCapture(window, ud, true);
        break;
    case GLFW_MOUSE_BUTTON_RIGHT:
        setCapture(window, ud, false);
        break;
    }
}

static void cbGLFWFocus(GLFWwindow *window, int focused) {
    userdata_t *ud = (userdata_t *)glfwGetWindowUserPointer(window);
    if (focused == GLFW_FALSE)
        setCapture(window, ud, false);
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

static void angle_vectors(vec3 degs, float *f, float *s, float *u) {
    float y = glm_rad(degs[0]);
    float p = glm_rad(degs[1]);
    float r = glm_rad(degs[2]);
    float sr = sin(r), sp = sin(p), sy = sin(y);
    float cr = cos(r), cp = cos(p), cy = cos(y);
    if (f) {
        f[0] = cp * cy;
        f[1] = cp * sy;
        f[2] = -sp;
    }
    if (s) {
        s[0] = -sr * sp * cy + cr * sy;
        s[1] = -sr * sp * sy - cr * cy;
        s[2] = -sr * cp;
    }
    if (u) {
        u[0] = cr * sp * cy + sr * sy;
        u[1] = cr * sp * sy - sr * cy;
        u[2] = cr * cp;
    }
}

static void mv_friction(vec3 vel, float dt, float stopspeed, float friction, float surfaceFriction) {
    float spd = glm_vec3_norm(vel);
    if (spd < 0.1)
        return;
    float ctrl = GLM_MAX(stopspeed, spd);
    float drop = dt * ctrl * friction * surfaceFriction;
    glm_vec3_scale(vel, GLM_MAX(0.0, spd - drop) / spd, vel);
}

static void mv_accelerate(vec3 vel, float dt, vec3 wishdir, float wishspd, float accel, float surfaceFriction) {
    float speed = glm_vec3_dot(vel, wishdir);
    float acc_1 = GLM_MAX(0.0, wishspd - speed);
    float acc_2 = dt * wishspd * accel * surfaceFriction;
    glm_vec3_muladds(wishdir, GLM_MIN(acc_1, acc_2), vel);
}

static void mv_airaccelerate(vec3 vel, float dt, vec3 wishdir, float wishspd, float aircap, float accel, float surfaceFriction) {
    wishspd = glm_clamp(wishspd, 0, aircap);
    float speed = glm_vec3_dot(vel, wishdir);
    float acc_1 = GLM_MAX(0.0, wishspd - speed);
    float acc_2 = dt * wishspd * accel * surfaceFriction;
    glm_vec3_muladds(wishdir, GLM_MIN(acc_1, acc_2), vel);
}

static void calc_wishvel(userdata_t *ud, vec3 wishdir, bool d3, float *wishspd,
                         float forwardspeed, float sidespeed, float duckmod, float maxspeed) {
    vec3 f, s;
    angle_vectors(ud->ang, f, s, NULL);
    if (!d3) {
        f[2] = 0.0;
        s[2] = 0.0;
    }
    glm_vec3_normalize(f);
    glm_vec3_normalize(s);
    glm_vec3_zero(wishdir);
    if (ud->w)
        glm_vec3_muladds(f, +forwardspeed, wishdir);
    if (ud->s)
        glm_vec3_muladds(f, -forwardspeed, wishdir);
    if (ud->a)
        glm_vec3_muladds(s, -sidespeed, wishdir);
    if (ud->d)
        glm_vec3_muladds(s, +sidespeed, wishdir);
    *wishspd = glm_vec3_norm(wishdir);
    glm_vec3_normalize(wishdir);
    *wishspd = glm_clamp(*wishspd, 0, maxspeed);
    if (ud->bDucked)
        *wishspd *= duckmod;
}

const float sv_gravity = 800.0;
const float sv_maxspeed = 250.0;
const float cl_sidespeed = 450.0;
const float cl_forwardspeed = 450.0;
const float sv_stopspeed = 100.0;
const float sv_friction = 4.0;
const float sv_accelerate = 5.0;
const float sv_airaccelerate = 100.0;
const float sv_jump_impulse = 301.993377;
const float duck_time = 0.125;

static void player_move(userdata_t *ud, float dt) {
    vec3 wishdir;
    float wishspd;
    calc_wishvel(ud, wishdir, ud->bNoclip, &wishspd, cl_forwardspeed, cl_sidespeed, 0.34, sv_maxspeed);

    if (ud->bNoclip) {
        glm_vec3_scale(wishdir, wishspd, ud->vel);
    } else {
        if (ud->bGround) {
            mv_friction(ud->vel, dt, sv_stopspeed, sv_friction, 1.0);
            mv_accelerate(ud->vel, dt, wishdir, wishspd, sv_accelerate, 1.0);
        } else {
            mv_airaccelerate(ud->vel, dt, wishdir, wishspd, 30.0, sv_airaccelerate, 1.0);
            ud->vel[2] -= sv_gravity * dt / 2;
        }
        {
            bool wantJump = ud->j || ud->sd;
            bool should_J = wantJump && !ud->bPrevJ;
            ud->bPrevJ = wantJump;
            ud->sd = false;
            if (ud->bGround && should_J) {
                fprintf(stderr, "prespeed: %.3f\n", glm_vec2_norm(ud->vel));
                glm_vec3_copy(ud->pos, ud->jumpOff);
                ud->vel[2] = sv_jump_impulse;
            }
        }
        {
            bool wantDuck = ud->c || ud->su;
            ud->su = false;
            if (wantDuck) {
                if (ud->bGround) {
                    ud->bInDuck = true;
                    ud->flDuckAmount = glm_clamp(ud->flDuckAmount + dt / duck_time, 0.0, 1.0);
                    if (ud->flDuckAmount == 1.0) {
                        ud->bInDuck = false;
                        if (!ud->bDucked) {
                            ud->bDucked = true;
                            ud->pos[2] -= 18.0;
                            ud->hull = 2;
                        }
                    }
                } else {
                    if (!ud->bDucked) {
                        ud->bDucked = true;
                        ud->bInDuck = false;
                        ud->hull = 2;
                        ud->flDuckAmount = 1.0;
                    }
                }
            } else {
                if (ud->bGround) {
                    // can unduck
                    if (PM_HullPointContents(ud->bsp, ud->bsp->hull[0], (vec3){ud->pos[0], ud->pos[1], ud->pos[2] + 18.0}) == CONTENTS_EMPTY) {
                        ud->flDuckAmount = glm_clamp(ud->flDuckAmount - dt / duck_time, 0.0, 1.0);
                        if (ud->bInDuck || ud->bDucked) {
                            ud->hull = 0;
                            ud->pos[2] += 18.0;
                            ud->bInDuck = false;
                            ud->bDucked = false;
                            fprintf(stderr, "unduck ground\n");
                        }
                    } else {
                        ud->flDuckAmount = glm_clamp(ud->flDuckAmount + dt / duck_time, 0.0, 1.0);
                        ud->bInDuck = true;
                    }
                } else {
                    // can unduck
                    if (ud->flDuckAmount > 0.0 && PM_HullPointContents(ud->bsp, ud->bsp->hull[0], ud->pos) == CONTENTS_EMPTY) {
                        ud->hull = 0;
                        ud->bDucked = false;
                        ud->bInDuck = false;
                        ud->flDuckAmount = 0.0;
                    }
                }
            }
        }
    }

    float frac = 0.0f;
    float curr_pos[3];
    float next_pos[3];
    glm_vec3_copy(ud->pos, curr_pos);
    for (int i = 0; i < 4; i++) {
        // next_pos if not clipped
        glm_vec3_copy(curr_pos, next_pos);
        glm_vec3_muladds(ud->vel, (1.0f - frac) * dt, next_pos);
        // trace curr_pos -> next_pos
        pmtrace_t trace;
        memset(&trace, 0, sizeof(trace));
        if (ud->bNoclip || PM_RecursiveHullCheck(ud->bsp, ud->bsp->hull[ud->hull], ud->bsp->hull[ud->hull], frac, 1.0f, curr_pos, next_pos, &trace)) { // full move
            if (trace.startsolid && !ud->bNoclip)
                break;
            frac = 1.0;
            glm_vec3_copy(next_pos, ud->pos);
            break;
        }
        // clip vel
        float backoff = glm_vec3_dot(trace.plane.n, ud->vel);
        if (backoff < 0) {
            // fprintf(stderr, "normal:\n");
            // glm_vec3_print(trace.plane.n, stderr);
            // fprintf(stderr, "speed:\n");
            // glm_vec3_print(ud->vel, stderr);
            // fprintf(stderr, "dist: %f\n", glm_vec3_dot(ud->pos, trace.plane.n) - trace.plane.d);
            glm_vec3_muladds(trace.plane.n, -backoff, ud->vel);
        }
        // use endpos as next_pos
        frac = trace.fraction;
        glm_vec3_copy(trace.endpos, next_pos);
        glm_vec3_copy(trace.endpos, ud->pos);
    }

    // if (frac < 1.0) {
    //     fprintf(stderr, "frac: %.3f\n", frac);
    //     glm_vec3_print(ud->vel, stderr);
    // }

    ud->bGround = false;
    if (!ud->bNoclip) {
        pmtrace_t trace;
        glm_vec3_add(ud->pos, (vec3){0.0, 0.0, -2.0}, next_pos);
        if (!PM_RecursiveHullCheck(ud->bsp, ud->bsp->hull[ud->hull], ud->bsp->hull[ud->hull], 0.0, 1.0, ud->pos, next_pos, &trace)) {
            if (trace.plane.n[2] > 0.7 && ud->vel[2] < 180.0)
                ud->bGround = true;
        }
        if (!ud->bGround)
            ud->vel[2] -= sv_gravity * dt / 2;
    }
}

int main(int argc, char **argv) {
    glfwSetErrorCallback(cbGlfwError);
    glfwInit();
    glfwWindowHint(GLFW_RESIZABLE, false);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    GLFWwindow *window = glfwCreateWindow(1280, 720, "GL Game", NULL, NULL);
    glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
    glfwMakeContextCurrent(window);
    glewExperimental = true;
    glewInit();
    glDebugMessageCallback(cbGLDebug, NULL);

    // fprintf(stderr, "GLEW_ARB_bindless_texture = %d\n", GLEW_ARB_bindless_texture);

    userdata_t ud;
    memset(&ud, 0, sizeof(ud));
    glfwSetWindowUserPointer(window, &ud);
    glfwSetKeyCallback(window, cbGLFWKey);
    glfwSetScrollCallback(window, cbGLFWScr);
    glfwSetCursorPosCallback(window, cbGLFWPos);
    glfwSetMouseButtonCallback(window, cbGLFWBtn);
    glfwSetWindowFocusCallback(window, cbGLFWFocus);

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
        fprintf(stderr, "clipnodes: %zu, planes: %zu\n", bsp.clip_cnt, bsp.planecnt);
        bspload = true;
        ud.bsp = &bsp;
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

    glfwSwapInterval(0);
    glClearColor(0.2f, 0.3f, 0.3f, 1.0f);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    // glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

    mat4 m_proj, m_view, m_mvp;

    int w, h;
    glfwGetWindowSize(window, &w, &h);
    glm_perspective(glm_rad(60.0), (float)w / (float)h, 8.0, 16384.0, m_proj);
    vec3 v_eye, v_lookat;
    double prevTime = glfwGetTime();
    double prevFpsX = glfwGetTime();
    int fps = 0;

    glActiveTexture(GL_TEXTURE0);

    fprintf(stderr, "HELLO: " __FILE__ " %d\n", __LINE__);

    while (glfwWindowShouldClose(window) == 0) {
        double currTime = glfwGetTime();
        double dt = currTime - prevTime;
        prevTime = currTime;
        fps++;
        if (currTime - prevFpsX >= 0.01) {
            char title[64];
            snprintf(title, sizeof(title), "GL Game (%d fps, ground %d, hull %d, duckamt %f)\n", (int)(fps / (currTime - prevFpsX)), ud.bGround, ud.hull, ud.flDuckAmount);
            glfwSetWindowTitle(window, title);
            prevFpsX = currTime;
            fps = 0;
        }
        glfwPollEvents();

        player_move(&ud, dt);

        v_eye[0] = ud.pos[0];
        v_eye[1] = ud.pos[1];
        v_eye[2] = ud.pos[2] - (ud.hull == 0 ? 36.0 : 18.0) + 36.0 * (1.0 - ud.flDuckAmount) + 28.0;

        angle_vectors(ud.ang, v_lookat, NULL, NULL);
        glm_vec3_add(v_eye, v_lookat, v_lookat);
        glm_lookat(v_eye, v_lookat, GLM_ZUP, m_view);
        glm_mat4_mul(m_proj, m_view, m_mvp);
        glUniformMatrix4fv(locMVP, 1, GL_FALSE, &m_mvp[0][0]);

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        for (uint32_t i = 0; i < bsp.text_cnt; i++) {
            ZigBSPTex bsptex = bsp.textures[i];
            if (bsptex.n3Indexs > 0 && !bsptex.skipped) {
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