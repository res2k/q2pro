/*
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

#pragma once

#include "common/cvar.h"
#include "common/error.h"

#define MAX_DLIGHTS     64
#define MAX_ENTITIES    2048
#define MAX_PARTICLES   8192
#define MAX_LIGHTSTYLES 256

#define POWERSUIT_SCALE     4.0f
#define WEAPONSHELL_SCALE   0.5f

#define RF_SHELL_MASK       (RF_SHELL_RED | RF_SHELL_GREEN | RF_SHELL_BLUE | \
                             RF_SHELL_DOUBLE | RF_SHELL_HALF_DAM | RF_SHELL_LITE_GREEN)

#define DLIGHT_CUTOFF       64

typedef struct entity_s {
    qhandle_t           model;          // opaque type outside refresh
    vec3_t              angles;

    /*
    ** most recent data
    */
    vec3_t              origin;     // also used as RF_BEAM's "from"
    unsigned            frame;      // also used as RF_BEAM's diameter

    /*
    ** previous data for lerping
    */
    vec3_t              oldorigin;  // also used as RF_BEAM's "to"
    unsigned            oldframe;

    /*
    ** misc
    */
    float   backlerp;               // 0.0 = current, 1.0 = old
    int     skinnum;                // also used as RF_BEAM's palette index,
                                    // -1 => use rgba

    float   alpha;                  // ignore if RF_TRANSLUCENT isn't set
    color_t rgba;

    qhandle_t   skin;           // NULL for inline skin
    int         flags;

    float       scale;
} entity_t;

typedef struct {
    vec3_t  origin;
    vec3_t  transformed;
    vec3_t  color;
    float   intensity;
} dlight_t;

typedef struct {
    vec3_t  origin;
    int     color;              // -1 => use rgba
    float   alpha;
    color_t rgba;
} particle_t;

typedef struct {
    float   white;              // highest of RGB
} lightstyle_t;

// rendered fog parameters
typedef struct {
    struct {
        float       r, g, b;
        float       density;
        float       sky_factor;
    } global;
    struct {
        struct {
            float   r, g, b;
            float   dist;
        } start, end;
        float   falloff;
        float   density;
    } height;
} fog_params_t;

typedef struct {
    int         x, y, width, height;// in virtual screen coordinates
    float       fov_x, fov_y;
    vec3_t      vieworg;
    vec3_t      viewangles;
    vec4_t      screen_blend;       // rgba 0-1 full screen blend
    vec4_t      damage_blend;       // rgba 0-1 damage blend
    float       time;               // time is uesed to auto animate
    int         rdflags;            // RDF_UNDERWATER, etc

    byte        *areabits;          // if not NULL, only areas with set bits will be drawn

    lightstyle_t    *lightstyles;   // [MAX_LIGHTSTYLES]

    int         num_entities;
    entity_t    *entities;

    int         num_dlights;
    dlight_t    *dlights;

    int         num_particles;
    particle_t  *particles;

    fog_params_t    fog;
} refdef_t;

typedef struct {
    int     colorbits;
    int     depthbits;
    int     stencilbits;
    int     multisamples;
    bool    debug;
} r_opengl_config_t;

typedef enum {
    QVF_FULLSCREEN      = BIT(0),
    QVF_GAMMARAMP       = BIT(1),
} vidFlags_t;

typedef struct {
    int         width;
    int         height;
    vidFlags_t  flags;
} refcfg_t;

extern refcfg_t r_config;

typedef struct {
    int left, right, top, bottom;
} clipRect_t;

typedef enum {
    IF_NONE         = 0,
    IF_PERMANENT    = BIT(0),
    IF_TRANSPARENT  = BIT(1),
    IF_PALETTED     = BIT(2),
    IF_UPSCALED     = BIT(3),
    IF_SCRAP        = BIT(4),
    IF_TURBULENT    = BIT(5),
    IF_REPEAT       = BIT(6),
    IF_NEAREST      = BIT(7),
    IF_OPAQUE       = BIT(8),
    IF_SPECIAL      = BIT(9),
} imageflags_t;

typedef enum {
    IT_PIC,
    IT_FONT,
    IT_SKIN,
    IT_SPRITE,
    IT_WALL,
    IT_SKY,
    IT_CLASSIC_SKY,

    IT_MAX
} imagetype_t;

// called when the library is loaded
bool    R_Init(bool total);

// called before the library is unloaded
void    R_Shutdown(bool total);

// All data that will be used in a level should be
// registered before rendering any frames to prevent disk hits,
// but they can still be registered at a later time
// if necessary.
//
// EndRegistration will free any remaining data that wasn't registered.
// Any model_s or skin_s pointers from before the BeginRegistration
// are no longer valid after EndRegistration.
//
// Skins and images need to be differentiated, because skins
// are flood filled to eliminate mip map edge errors, and pics have
// an implicit "pics/" prepended to the name. (a pic name that starts with a
// slash will not use the "pics/" prefix or the ".pcx" postfix)
void    R_BeginRegistration(const char *map);
qhandle_t R_RegisterModel(const char *name);
qhandle_t R_RegisterImage(const char *name, imagetype_t type,
                          imageflags_t flags);
void    R_SetSky(const char *name, float rotate, bool autorotate, const vec3_t axis);
bool    R_SetClassicSky(const char *name);
void    R_EndRegistration(void);

#define R_RegisterPic(name)     R_RegisterImage(name, IT_PIC, IF_PERMANENT)
#define R_RegisterTempPic(name) R_RegisterImage(name, IT_PIC, IF_NONE)
#define R_RegisterFont(name)    R_RegisterImage(name, IT_FONT, IF_PERMANENT)
#define R_RegisterSkin(name)    R_RegisterImage(name, IT_SKIN, IF_NONE)
#define R_RegisterSprite(name)  R_RegisterImage(name, IT_SPRITE, IF_NONE)

void    R_RenderFrame(const refdef_t *fd);
void    R_LightPoint(const vec3_t origin, vec3_t light);

void    R_ClearColor(void);
void    R_SetAlpha(float alpha);
void    R_SetColor(uint32_t color);
void    R_SetClipRect(const clipRect_t *clip);
float   R_ClampScale(cvar_t *var);
void    R_SetScale(float scale);
void    R_DrawChar(int x, int y, int flags, int ch, qhandle_t font);
void    R_DrawStretchChar(int x, int y, int w, int h, int flags, int ch, qhandle_t font);
int     R_DrawString(int x, int y, int flags, size_t maxChars,
                     const char *string, qhandle_t font);  // returns advanced x coord

// kfont stuff
typedef struct {
    uint16_t    x, y, w, h;
} kfont_char_t;

#define KFONT_ASCII_MIN         32
#define KFONT_ASCII_MAX         126

typedef struct {
    qhandle_t       pic;
    kfont_char_t    chars[KFONT_ASCII_MAX - KFONT_ASCII_MIN + 1];
    uint16_t        line_height;
    float           sw, sh;
} kfont_t;

const kfont_char_t *SCR_KFontLookup(const kfont_t *kfont, uint32_t codepoint);
void    SCR_LoadKFont(kfont_t *font, const char *filename);
int     R_DrawKFontChar(int x, int y, int scale, int flags, uint32_t codepoint, const kfont_t *kfont);

bool    R_GetPicSize(int *w, int *h, qhandle_t pic);   // returns transparency bit
void    R_DrawPic(int x, int y, qhandle_t pic);
void    R_DrawStretchPic(int x, int y, int w, int h, qhandle_t pic);
void    R_DrawStretchRotatePic(int x, int y, int w, int h, float angle, int pivot_x, int pivot_y, qhandle_t pic);
void    R_DrawKeepAspectPic(int x, int y, int w, int h, qhandle_t pic);
void    R_DrawStretchRaw(int x, int y, int w, int h);
void    R_UpdateRawPic(int pic_w, int pic_h, const uint32_t *pic);
void    R_TileClear(int x, int y, int w, int h, qhandle_t pic);
void    R_DrawFill8(int x, int y, int w, int h, int c);
void    R_DrawFill32(int x, int y, int w, int h, uint32_t color);

void    R_AddDebugLine(const vec3_t start, const vec3_t end, color_t color, int time, bool depth_test);
void    R_AddDebugPoint(const vec3_t point, float size, color_t color, int time, bool depth_test);
void    R_AddDebugAxis(const vec3_t point, float size, int time, bool depth_test);
void    R_AddDebugCircle(const vec3_t origin, float radius, color_t color, int time, bool depth_test);
void    R_AddDebugBounds(const vec3_t absmins, const vec3_t absmaxs, color_t color, int time, bool depth_test);
void    R_AddDebugSphere(const vec3_t origin, float radius, color_t color, int time, bool depth_test);
// angles = null to make oriented text
void    R_AddDebugText(const vec3_t origin, const char *text, float size, const vec3_t angles, color_t color, int time, bool depth_test);
void    R_AddDebugCylinder(const vec3_t origin, float half_height, float radius, color_t color, int time, bool depth_test);
void    R_AddDebugRay(const vec3_t start, const vec3_t dir, float length, float size, color_t line_color, color_t arrow_color, int time, bool depth_test);
void    R_AddDebugArrow(const vec3_t start, const vec3_t end, float size, color_t line_color, color_t arrow_color, int time, bool depth_test);

void    R_AddDebugCurveArrow(const vec3_t start, const vec3_t ctrl, const vec3_t end, float size, color_t line_color, color_t arrow_color, int time, bool depth_test);

// video mode and refresh state management entry points
void    R_BeginFrame(void);
void    R_EndFrame(void);
void    R_ModeChanged(int width, int height, int flags);

void R_GetGLConfig(r_opengl_config_t *cfg);
