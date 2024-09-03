/*
Copyright (C) 2003-2006 Andrey Nazarov
Copyright (C) 1997-2001 Id Software, Inc.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include "gl.h"
#include "shared/list.h"
#include "client/client.h"
#include "common/prompt.h"
#include "debug_fonts/cursive.h"
#include "debug_fonts/futural.h"
#include "debug_fonts/futuram.h"
#include "debug_fonts/gothgbt.h"
#include "debug_fonts/gothgrt.h"
#include "debug_fonts/gothiceng.h"
#include "debug_fonts/gothicger.h"
#include "debug_fonts/gothicita.h"
#include "debug_fonts/gothitt.h"
#include "debug_fonts/rowmand.h"
#include "debug_fonts/rowmans.h"
#include "debug_fonts/rowmant.h"
#include "debug_fonts/scriptc.h"
#include "debug_fonts/scripts.h"
#include "debug_fonts/timesi.h"
#include "debug_fonts/timesib.h"
#include "debug_fonts/timesr.h"
#include "debug_fonts/timesrb.h"
#include <assert.h>

typedef struct debug_font_s {
    // Number of glyphs
    int count;
    // Font height
    char height;
    // Widths of the glyphs
    const char* width;
    // Real widths of the glyphs (calculated from data)
    const char* realwidth;
    // Number of chars in each glyph
    const int* size;
    // Pointers to glyph data
    const char **glyph_data;
} debug_font_t;

#define DEBUG_FONT(NAME)        \
    {                           \
        #NAME,                  \
        {                       \
            NAME##_count,       \
            NAME##_height,      \
            NAME##_width,       \
            NAME##_realwidth,   \
            NAME##_size,        \
            NAME                \
        }                       \
    }

static const struct {
    const char *name;
    debug_font_t font;
} debug_fonts[] = {
    DEBUG_FONT(futural),
    DEBUG_FONT(cursive),
    DEBUG_FONT(futuram),
    DEBUG_FONT(gothgbt),
    DEBUG_FONT(gothgrt),
    DEBUG_FONT(gothiceng),
    DEBUG_FONT(gothicger),
    DEBUG_FONT(gothicita),
    DEBUG_FONT(gothitt),
    DEBUG_FONT(rowmand),
    DEBUG_FONT(rowmans),
    DEBUG_FONT(rowmant),
    DEBUG_FONT(scriptc),
    DEBUG_FONT(scripts),
    DEBUG_FONT(timesi),
    DEBUG_FONT(timesib),
    DEBUG_FONT(timesr),
    DEBUG_FONT(timesrb),
};

#undef DEBUG_FONT

static const debug_font_t *dbg_font;

static cvar_t *gl_debug_font;
static cvar_t *gl_debug_linewidth;

#define MAX_DEBUG_LINES		8192

typedef struct debug_line_s {
    list_t          entry;
    vec3_t          start, end;
    uint32_t        color;
    uint32_t        time;
    glStateBits_t   bits;
} debug_line_t;

static debug_line_t debug_lines[MAX_DEBUG_LINES];
static list_t debug_lines_free;
static list_t debug_lines_active;

static cvar_t *gl_debug_linewidth;

void R_ClearDebugLines(void)
{
    List_Init(&debug_lines_free);
    List_Init(&debug_lines_active);
}

void R_AddDebugLine(const vec3_t start, const vec3_t end, uint32_t color, uint32_t time, qboolean depth_test)
{
    debug_line_t *l = LIST_FIRST(debug_line_t, &debug_lines_free, entry);

    if (LIST_EMPTY(&debug_lines_free)) {
        if (LIST_EMPTY(&debug_lines_active)) {
            for (int i = 0; i < MAX_DEBUG_LINES; i++)
                List_Append(&debug_lines_free, &debug_lines[i].entry);
        } else {
            debug_line_t *next;
            LIST_FOR_EACH_SAFE(debug_line_t, l, next, &debug_lines_active, entry) {
                if (l->time <= com_localTime2) {
                    List_Remove(&l->entry);
                    List_Insert(&debug_lines_free, &l->entry);
                }
            }
        }

        if (LIST_EMPTY(&debug_lines_free))
            l = LIST_FIRST(debug_line_t, &debug_lines_active, entry);
        else
            l = LIST_FIRST(debug_line_t, &debug_lines_free, entry);
    }

    // unlink from freelist
    List_Remove(&l->entry);
    List_Append(&debug_lines_active, &l->entry);

    VectorCopy(start, l->start);
    VectorCopy(end, l->end);
    l->color = color;
    l->time = com_localTime2 + time;
    if (l->time < com_localTime2)
        l->time = UINT32_MAX;
    l->bits = GLS_DEPTHMASK_FALSE;
    if (!depth_test)
        l->bits |= GLS_DEPTHTEST_DISABLE;
    if (gl_config.caps & QGL_CAP_LINE_SMOOTH)
        l->bits |= GLS_BLEND_BLEND;
}

#define GL_DRAWLINE(sx, sy, sz, ex, ey, ez) \
    R_AddDebugLine((const vec3_t) { (sx), (sy), (sz) }, (const vec3_t) { (ex), (ey), (ez) }, color, time, depth_test)

#define GL_DRAWLINEV(s, e) \
    R_AddDebugLine(s, e, color, time, depth_test)

void R_AddDebugPoint(const vec3_t point, float size, uint32_t color, uint32_t time, qboolean depth_test)
{
    size *= 0.5f;
    GL_DRAWLINE(point[0] - size, point[1], point[2], point[0] + size, point[1], point[2]);
    GL_DRAWLINE(point[0], point[1] - size, point[2], point[0], point[1] + size, point[2]);
    GL_DRAWLINE(point[0], point[1], point[2] - size, point[0], point[1], point[2] + size);
}

void R_AddDebugAxis(const vec3_t origin, const vec3_t angles, float size, uint32_t time, qboolean depth_test)
{
    vec3_t axis[3], end;
    uint32_t color;

    if (angles) {
        AnglesToAxis(angles, axis);
    } else {
        VectorSet(axis[0], 1, 0, 0);
        VectorSet(axis[1], 0, 1, 0);
        VectorSet(axis[2], 0, 0, 1);
    }

    color = U32_RED;
    VectorMA(origin, size, axis[0], end);
    GL_DRAWLINEV(origin, end);

    color = U32_GREEN;
    VectorMA(origin, size, axis[1], end);
    GL_DRAWLINEV(origin, end);

    color = U32_BLUE;
    VectorMA(origin, size, axis[2], end);
    GL_DRAWLINEV(origin, end);
}

void R_AddDebugBounds(const vec3_t mins, const vec3_t maxs, uint32_t color, uint32_t time, qboolean depth_test)
{
    for (int i = 0; i < 4; i++) {
        // draw column
        float x = ((i > 1) ? mins : maxs)[0];
        float y = ((((i + 1) % 4) > 1) ? mins : maxs)[1];
        GL_DRAWLINE(x, y, mins[2], x, y, maxs[2]);

        // draw bottom & top
        int n = (i + 1) % 4;
        float x2 = ((n > 1) ? mins : maxs)[0];
        float y2 = ((((n + 1) % 4) > 1) ? mins : maxs)[1];
        GL_DRAWLINE(x, y, mins[2], x2, y2, mins[2]);
        GL_DRAWLINE(x, y, maxs[2], x2, y2, maxs[2]);
    }
}

// https://danielsieger.com/blog/2021/03/27/generating-spheres.html
void R_AddDebugSphere(const vec3_t origin, float radius, uint32_t color, uint32_t time, qboolean depth_test)
{
    vec3_t verts[160];
    const int n_stacks = min(4 + radius / 32, 10);
    const int n_slices = min(6 + radius / 32, 16);
    const int v0 = 0;
    int v1 = 1;

    for (int i = 0; i < n_stacks - 1; i++) {
        float phi = M_PIf * (i + 1) / n_stacks;
        for (int j = 0; j < n_slices; j++) {
            float theta = 2 * M_PIf * j / n_slices;
            vec3_t v = {
                sinf(phi) * cosf(theta),
                sinf(phi) * sinf(theta),
                cosf(phi)
            };
            VectorMA(origin, radius, v, verts[v1]);
            v1++;
        }
    }

    VectorCopy(origin, verts[v0]);
    VectorCopy(origin, verts[v1]);

    verts[v0][2] += radius;
    verts[v1][2] -= radius;

    for (int i = 0; i < n_slices; i++) {
        int i0 = i + 1;
        int i1 = (i + 1) % n_slices + 1;
        GL_DRAWLINEV(verts[v0], verts[i1]);
        GL_DRAWLINEV(verts[i1], verts[i0]);
        GL_DRAWLINEV(verts[i0], verts[v0]);
        i0 = i + n_slices * (n_stacks - 2) + 1;
        i1 = (i + 1) % n_slices + n_slices * (n_stacks - 2) + 1;
        GL_DRAWLINEV(verts[v1], verts[i0]);
        GL_DRAWLINEV(verts[i0], verts[i1]);
        GL_DRAWLINEV(verts[i1], verts[v1]);
    }

    for (int j = 0; j < n_stacks - 2; j++) {
        int j0 = j * n_slices + 1;
        int j1 = (j + 1) * n_slices + 1;
        for (int i = 0; i < n_slices; i++) {
            int i0 = j0 + i;
            int i1 = j0 + (i + 1) % n_slices;
            int i2 = j1 + (i + 1) % n_slices;
            int i3 = j1 + i;
            GL_DRAWLINEV(verts[i0], verts[i1]);
            GL_DRAWLINEV(verts[i1], verts[i2]);
            GL_DRAWLINEV(verts[i2], verts[i3]);
            GL_DRAWLINEV(verts[i3], verts[i0]);
        }
    }
}

void R_AddDebugCircle(const vec3_t origin, float radius, uint32_t color, uint32_t time, qboolean depth_test)
{
    int vert_count = min(5 + radius / 8, 16);
    float rads = (2 * M_PIf) / vert_count;

    for (int i = 0; i < vert_count; i++) {
        float a = i * rads;
        float c = cosf(a);
        float s = sinf(a);
        float x = c * radius + origin[0];
        float y = s * radius + origin[1];

        a = ((i + 1) % vert_count) * rads;
        c = cosf(a);
        s = sinf(a);
        float x2 = c * radius + origin[0];
        float y2 = s * radius + origin[1];

        GL_DRAWLINE(x, y, origin[2], x2, y2, origin[2]);
    }
}

void R_AddDebugCylinder(const vec3_t origin, float half_height, float radius, uint32_t color, uint32_t time, qboolean depth_test)
{
    int vert_count = min(5 + radius / 8, 16);
    float rads = (2 * M_PIf) / vert_count;

    for (int i = 0; i < vert_count; i++) {
        float a = i * rads;
        float c = cosf(a);
        float s = sinf(a);
        float x = c * radius + origin[0];
        float y = s * radius + origin[1];

        a = ((i + 1) % vert_count) * rads;
        c = cosf(a);
        s = sinf(a);
        float x2 = c * radius + origin[0];
        float y2 = s * radius + origin[1];

        GL_DRAWLINE(x, y, origin[2] - half_height, x2, y2, origin[2] - half_height);
        GL_DRAWLINE(x, y, origin[2] + half_height, x2, y2, origin[2] + half_height);
        GL_DRAWLINE(x, y, origin[2] - half_height, x,  y,  origin[2] + half_height);
    }
}

void R_DrawArrowCap(const vec3_t apex, const vec3_t dir, float size,
                    uint32_t color, uint32_t time, qboolean depth_test)
{
    vec3_t cap_end;
    VectorMA(apex, size, dir, cap_end);

    R_AddDebugLine(apex, cap_end, color, time, depth_test);

    vec3_t right, up;
    MakeNormalVectors(dir, right, up);

    vec3_t l;
    VectorMA(apex, size, right, l);
    R_AddDebugLine(l, cap_end, color, time, depth_test);

    VectorMA(apex, -size, right, l);
    R_AddDebugLine(l, cap_end, color, time, depth_test);
}

void R_AddDebugArrow(const vec3_t start, const vec3_t end, float size, uint32_t line_color,
                     uint32_t arrow_color, uint32_t time, qboolean depth_test)
{
    vec3_t dir;
    VectorSubtract(end, start, dir);
    float len = VectorNormalize(dir);

    if (len > size) {
        vec3_t line_end;
        VectorMA(start, len - size, dir, line_end);
        R_AddDebugLine(start, line_end, line_color, time, depth_test);
        R_DrawArrowCap(line_end, dir, size, arrow_color, time, depth_test);
    } else {
        R_DrawArrowCap(end, dir, len, arrow_color, time, depth_test);
    }
}

void R_AddDebugCurveArrow(const vec3_t start, const vec3_t ctrl, const vec3_t end, float size,
                          uint32_t line_color, uint32_t arrow_color, uint32_t time, qboolean depth_test)
{
    int num_points = Q_clip(Distance(start, end) / 32, 3, 24);
    vec3_t last_point;

    for (int i = 0; i <= num_points; i++) {
        float t = i / (float)num_points;
        float it = 1.0f - t;

        float a = it * it;
        float b = 2.0f * t * it;
        float c = t * t;

        vec3_t p = {
            a * start[0] + b * ctrl[0] + c * end[0],
            a * start[1] + b * ctrl[1] + c * end[1],
            a * start[2] + b * ctrl[2] + c * end[2]
        };

        if (i == num_points)
            R_AddDebugArrow(last_point, p, size, line_color, arrow_color, time, depth_test);
        else if (i)
            R_AddDebugLine(last_point, p, line_color, time, depth_test);

        VectorCopy(p, last_point);
    }
}

void R_AddDebugRay(const vec3_t start, const vec3_t dir, float length, float size, uint32_t line_color, uint32_t arrow_color, uint32_t time, qboolean depth_test)
{
    if (length > size) {
        vec3_t line_end;
        VectorMA(start, length - size, dir, line_end);

        R_AddDebugLine(start, line_end, line_color, time, depth_test);
        R_DrawArrowCap(line_end, dir, size, arrow_color, time, depth_test);
    } else {
        R_DrawArrowCap(start, dir, length, arrow_color, time, depth_test);
    }
}

void R_AddDebugText(const vec3_t origin, const vec3_t angles, const char *text, float size, uint32_t color, uint32_t time, qboolean depth_test)
{
    int total_lines = 1;
    float scale = (1.0f / dbg_font->height) * (size * 32);

    int l = strlen(text);

    for (int i = 0; i < l; i++) {
        if (text[i] == '\n')
            total_lines++;
    }

    if (!angles)
    {
        vec3_t d;
        VectorSubtract(origin, glr.fd.vieworg, d);
        VectorNormalize(d);
        d[2] = 0.0f;
        vectoangles2(d, d);
        angles = (const vec_t *) &d;
    }

    vec3_t right, up;
    AngleVectors(angles, NULL, right, up);

    float y_offset = -((dbg_font->height * scale) * 0.5f) * total_lines;

    const char *c = text;
    for (int line = 0; line < total_lines; line++) {
        const char *c_end = c;
        float width = 0;

        for (; *c_end && *c_end != '\n'; c_end++) {
            width += dbg_font->width[*c_end - ' '] * scale;
        }
        
        float x_offset = (width * 0.5f);

        for (const char *rc = c; rc != c_end; rc++) {
            char c = *rc - ' ';
            const float char_width = dbg_font->width[(int)c] * scale;
            const int char_size = dbg_font->size[(int)c];
            const char *char_data = dbg_font->glyph_data[(int)c];

            for (int i = 0; i < char_size; i += 4) {
                vec3_t s;
                float r = -char_data[i] * scale + x_offset;
                float u = -(char_data[i + 1] * scale + y_offset);
                VectorMA(origin, -r, right, s);
                VectorMA(s, u, up, s);
                vec3_t e;
                r = -char_data[i + 2] * scale + x_offset;
                u = -(char_data[i + 3] * scale + y_offset);
                VectorMA(origin, -r, right, e);
                VectorMA(e, u, up, e);
                GL_DRAWLINEV(s, e);
            }

            x_offset -= char_width;
        }

        y_offset += dbg_font->height * scale;

        c = c_end + 1;
    }
}

void GL_DrawDebugLines(void)
{
    glStateBits_t bits = -1;
    debug_line_t *l, *next;
    GLfloat *dst_vert;
    int numverts;

    if (LIST_EMPTY(&debug_lines_active))
        return;

    GL_LoadMatrix(NULL, glr.viewmatrix);
    GL_BindTexture(TMU_TEXTURE, TEXNUM_WHITE);
    GL_BindArrays(VA_NULLMODEL);
    GL_ArrayBits(GLA_VERTEX | GLA_COLOR);

    if (qglLineWidth)
        qglLineWidth(gl_debug_linewidth->value);

    if (gl_config.caps & QGL_CAP_LINE_SMOOTH)
        qglEnable(GL_LINE_SMOOTH);

    static_assert(q_countof(debug_lines) <= q_countof(tess.vertices) / 8, "Too many debug lines");

    dst_vert = tess.vertices;
    numverts = 0;
    LIST_FOR_EACH_SAFE(debug_line_t, l, next, &debug_lines_active, entry) {
        if (l->time < com_localTime2) { // expired
            List_Remove(&l->entry);
            List_Insert(&debug_lines_free, &l->entry);
            continue;
        }

        if (bits != l->bits) {
            if (numverts) {
                GL_LockArrays(numverts);
                qglDrawArrays(GL_LINES, 0, numverts);
                GL_UnlockArrays();
            }

            GL_StateBits(l->bits);
            bits = l->bits;

            dst_vert = tess.vertices;
            numverts = 0;
        }

        VectorCopy(l->start, dst_vert);
        VectorCopy(l->end, dst_vert + 4);
        WN32(dst_vert + 3, l->color);
        WN32(dst_vert + 7, l->color);
        dst_vert += 8;

        numverts += 2;
    }

    if (numverts) {
        GL_LockArrays(numverts);
        qglDrawArrays(GL_LINES, 0, numverts);
        GL_UnlockArrays();
    }

    if (gl_config.caps & QGL_CAP_LINE_SMOOTH)
        qglDisable(GL_LINE_SMOOTH);

    if (qglLineWidth)
        qglLineWidth(1.0f);
}

static void gl_debug_font_changed(cvar_t* cvar)
{
    int font_idx = -1;
    for (int i = 0; i < q_countof(debug_fonts); i++) {
        if (Q_strcasecmp(cvar->string, debug_fonts[i].name) == 0) {
            font_idx = i;
            break;
        }
    }
    if (font_idx < 0) {
        Com_WPrintf("unknown debug font: %s\n", cvar->string);
        font_idx = 0;
    }
    dbg_font = &debug_fonts[font_idx].font;
}

static void gl_debug_font_generator(struct genctx_s *gen)
{
    for (int i = 0; i < q_countof(debug_fonts); i++) {
        Prompt_AddMatch(gen, debug_fonts[i].name);
    }
}

void GL_InitDebugDraw(void)
{
    R_ClearDebugLines();

    gl_debug_linewidth = Cvar_Get("gl_debug_linewidth", "2", 0);
    gl_debug_font = Cvar_Get("gl_debug_font", debug_fonts[0].name, 0);
    gl_debug_font->changed = gl_debug_font_changed;
    gl_debug_font->generator = gl_debug_font_generator;
    gl_debug_font_changed(gl_debug_font);

    Cmd_AddCommand("cleardebuglines", R_ClearDebugLines);
}

void GL_ShutdownDebugDraw(void)
{
    Cmd_RemoveCommand("cleardebuglines");
}
