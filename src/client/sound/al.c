/*
Copyright (C) 2010 Andrey Nazarov

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

#include "sound.h"
#include "qal.h"
#include "common/json.h"

// translates from AL coordinate system to quake
#define AL_UnpackVector(v)  -v[1],v[2],-v[0]
#define AL_CopyVector(a,b)  ((b)[0]=-(a)[1],(b)[1]=(a)[2],(b)[2]=-(a)[0])

// OpenAL implementation should support at least this number of sources
#define MIN_CHANNELS    16

static cvar_t       *al_reverb;
static cvar_t       *al_reverb_lerp_time;

static cvar_t       *al_timescale;
static cvar_t       *al_merge_looping;

static ALuint       *s_srcnums;
static int          s_numalsources;
static ALuint       s_stream;
static ALuint       s_stream_buffers;
static ALboolean    s_loop_points;
static ALboolean    s_source_spatialize;
static unsigned     s_framecount;

static ALuint       s_underwater_filter;
static bool         s_underwater_flag;

// reverb stuff
typedef struct {
    char    material[16];
    int16_t step_id;
} al_reverb_material_t;

typedef struct {
    al_reverb_material_t    *materials; // if null, matches everything
    size_t                  num_materials;
    uint8_t                 preset;
} al_reverb_entry_t;

typedef struct {
    float               dimension;
    al_reverb_entry_t   *reverbs;
    size_t              num_reverbs;
} al_reverb_environment_t;

static size_t                   s_num_reverb_environments;
static al_reverb_environment_t  *s_reverb_environments;

static ALuint       s_reverb_effect;
static ALuint       s_reverb_slot;

static const EFXEAXREVERBPROPERTIES s_reverb_parameters[] = {
    EFX_REVERB_PRESET_GENERIC,
    EFX_REVERB_PRESET_PADDEDCELL,
    EFX_REVERB_PRESET_ROOM,
    EFX_REVERB_PRESET_BATHROOM,
    EFX_REVERB_PRESET_LIVINGROOM,
    EFX_REVERB_PRESET_STONEROOM,
    EFX_REVERB_PRESET_AUDITORIUM,
    EFX_REVERB_PRESET_CONCERTHALL,
    EFX_REVERB_PRESET_CAVE,
    EFX_REVERB_PRESET_ARENA,
    EFX_REVERB_PRESET_HANGAR,
    EFX_REVERB_PRESET_CARPETEDHALLWAY,
    EFX_REVERB_PRESET_HALLWAY,
    EFX_REVERB_PRESET_STONECORRIDOR,
    EFX_REVERB_PRESET_ALLEY,
    EFX_REVERB_PRESET_FOREST,
    EFX_REVERB_PRESET_CITY,
    EFX_REVERB_PRESET_MOUNTAINS,
    EFX_REVERB_PRESET_QUARRY,
    EFX_REVERB_PRESET_PLAIN,
    EFX_REVERB_PRESET_PARKINGLOT,
    EFX_REVERB_PRESET_SEWERPIPE,
    EFX_REVERB_PRESET_UNDERWATER,
    EFX_REVERB_PRESET_DRUGGED,
    EFX_REVERB_PRESET_DIZZY,
    EFX_REVERB_PRESET_PSYCHOTIC
};

static EFXEAXREVERBPROPERTIES   s_active_reverb;
static EFXEAXREVERBPROPERTIES   s_reverb_lerp_to, s_reverb_lerp_result;
static int                      s_reverb_lerp_start, s_reverb_lerp_time;
static uint8_t                  s_reverb_current_preset;

static const char *const s_reverb_names[] = {
    "generic",
    "padded_cell",
    "room",
    "bathroom",
    "living_room",
    "stone_room",
    "auditorium",
    "concert_hall",
    "cave",
    "arena",
    "hangar",
    "carpeted_hallway",
    "hallway",
    "stone_corridor",
    "alley",
    "forest",
    "city",
    "mountains",
    "quarry",
    "plain",
    "parking_lot",
    "sewer_pipe",
    "underwater",
    "drugged",
    "dizzy",
    "psychotic"
};

static void AL_LoadEffect(const EFXEAXREVERBPROPERTIES *reverb)
{
    qalEffectf(s_reverb_effect, AL_EAXREVERB_DENSITY, reverb->flDensity);
    qalEffectf(s_reverb_effect, AL_EAXREVERB_DIFFUSION, reverb->flDiffusion);
    qalEffectf(s_reverb_effect, AL_EAXREVERB_GAIN, reverb->flGain);
    qalEffectf(s_reverb_effect, AL_EAXREVERB_GAINHF, reverb->flGainHF);
    qalEffectf(s_reverb_effect, AL_EAXREVERB_GAINLF, reverb->flGainLF);
    qalEffectf(s_reverb_effect, AL_EAXREVERB_DECAY_TIME, reverb->flDecayTime);
    qalEffectf(s_reverb_effect, AL_EAXREVERB_DECAY_HFRATIO, reverb->flDecayHFRatio);
    qalEffectf(s_reverb_effect, AL_EAXREVERB_DECAY_LFRATIO, reverb->flDecayLFRatio);
    qalEffectf(s_reverb_effect, AL_EAXREVERB_REFLECTIONS_GAIN, reverb->flReflectionsGain);
    qalEffectf(s_reverb_effect, AL_EAXREVERB_REFLECTIONS_DELAY, reverb->flReflectionsDelay);
    qalEffectfv(s_reverb_effect, AL_EAXREVERB_REFLECTIONS_PAN, reverb->flReflectionsPan);
    qalEffectf(s_reverb_effect, AL_EAXREVERB_LATE_REVERB_GAIN, reverb->flLateReverbGain);
    qalEffectf(s_reverb_effect, AL_EAXREVERB_LATE_REVERB_DELAY, reverb->flLateReverbDelay);
    qalEffectfv(s_reverb_effect, AL_EAXREVERB_LATE_REVERB_PAN, reverb->flLateReverbPan);
    qalEffectf(s_reverb_effect, AL_EAXREVERB_ECHO_TIME, reverb->flEchoTime);
    qalEffectf(s_reverb_effect, AL_EAXREVERB_ECHO_DEPTH, reverb->flEchoDepth);
    qalEffectf(s_reverb_effect, AL_EAXREVERB_MODULATION_TIME, reverb->flModulationTime);
    qalEffectf(s_reverb_effect, AL_EAXREVERB_MODULATION_DEPTH, reverb->flModulationDepth);
    qalEffectf(s_reverb_effect, AL_EAXREVERB_AIR_ABSORPTION_GAINHF, reverb->flAirAbsorptionGainHF);
    qalEffectf(s_reverb_effect, AL_EAXREVERB_HFREFERENCE, reverb->flHFReference);
    qalEffectf(s_reverb_effect, AL_EAXREVERB_LFREFERENCE, reverb->flLFReference);
    qalEffectf(s_reverb_effect, AL_EAXREVERB_ROOM_ROLLOFF_FACTOR, reverb->flRoomRolloffFactor);
    qalEffecti(s_reverb_effect, AL_EAXREVERB_DECAY_HFLIMIT, reverb->iDecayHFLimit);

    qalAuxiliaryEffectSloti(s_reverb_slot, AL_EFFECTSLOT_EFFECT, s_reverb_effect);
}

static const vec3_t             s_reverb_probes[] = {
    { 0.00000000f,    0.00000000f,     -1.00000000f },
    { 0.00000000f,    0.00000000f,     1.00000000f },
    { 0.707106769f,   0.00000000f,     0.707106769f },
    { 0.353553385f,   0.612372458f,    0.707106769f },
    { -0.353553444f,  0.612372458f,    0.707106769f },
    { -0.707106769f, -6.18172393e-08f, 0.707106769f },
    { -0.353553325f, -0.612372518f,    0.707106769f },
    { 0.353553355f,  -0.612372458f,    0.707106769f },
    { 1.00000000f,   0.00000000f,      -4.37113883e-08f },
    { 0.499999970f,  0.866025448f,     -4.37113883e-08f },
    { -0.500000060f, 0.866025388f,     -4.37113883e-08f },
    { -1.00000000f,  -8.74227766e-08f, -4.37113883e-08f },
    { -0.499999911f, -0.866025448f,    -4.37113883e-08f },
    { 0.499999911f,  -0.866025448f,    -4.37113883e-08f },
};
static int                      s_reverb_probe_time;
static int                      s_reverb_probe_index;
static vec3_t                   s_reverb_probe_results[q_countof(s_reverb_probes)];
static float                    s_reverb_probe_avg;

static const al_reverb_environment_t  *s_reverb_active_environment;

static bool AL_EstimateDimensions(void)
{
    if (!s_reverb_environments)
        return false;

    if (s_reverb_probe_time > cl.time)
        return false;

    s_reverb_probe_time = cl.time + 13;
    vec3_t end;
    VectorMA(listener_origin, 8192.0f, s_reverb_probes[s_reverb_probe_index], end);

    trace_t tr;
    CL_Trace(&tr, listener_origin, end, vec3_origin, vec3_origin, NULL, MASK_SOLID);

    VectorSubtract(tr.endpos, listener_origin, s_reverb_probe_results[s_reverb_probe_index]);

    if (s_reverb_probe_index == 1 && (tr.surface->flags & SURF_SKY)) {
        s_reverb_probe_results[s_reverb_probe_index][2] += 4096.f;
    }

    vec3_t mins, maxs;
    ClearBounds(mins, maxs);

    for (size_t i = 0; i < q_countof(s_reverb_probes); i++)
        AddPointToBounds(s_reverb_probe_results[i], mins, maxs);

    vec3_t extents;
    VectorSubtract(maxs, mins, extents);

    s_reverb_probe_avg = (extents[0] + extents[1] + extents[2]) / 3.0f;

    s_reverb_probe_index = (s_reverb_probe_index + 1) % q_countof(s_reverb_probes);

    // check if we expanded or shrank the environment
    bool changed = false;

    while (s_reverb_active_environment != s_reverb_environments + s_num_reverb_environments - 1 &&
        s_reverb_probe_avg > s_reverb_active_environment->dimension) {
        s_reverb_active_environment++;
        changed = true;
    }

    if (!changed) {
        while (s_reverb_active_environment != s_reverb_environments && s_reverb_probe_avg < (s_reverb_active_environment - 1)->dimension) {
            s_reverb_active_environment--;
            changed = true;
        }
    }

    return changed;
}

static inline float AL_CalculateReverbFrac(void)
{
    float frac = (cl.time - (float) s_reverb_lerp_start) / (s_reverb_lerp_time - (float) s_reverb_lerp_start);
    float bfrac = 1.0f - frac;
    float f = Q_clipf(1.0f - (bfrac * bfrac * bfrac), 0.0f, 1.0f);
    return f;
}

static void AL_UpdateReverb(void)
{
    if (!s_reverb_environments)
        return;

    if (!cl.bsp)
        return;

    AL_EstimateDimensions();

    trace_t tr;
    const vec3_t mins = { -16, -16, 0 };
    const vec3_t maxs = { 16, 16, 0 };
    const vec3_t listener_start = { listener_origin[0], listener_origin[1], listener_origin[2] + 1.0f };
    const vec3_t listener_down = { listener_start[0], listener_start[1], listener_start[2] - 256.0f };
    CL_Trace(&tr, listener_start, mins, maxs, listener_down, NULL, MASK_SOLID);

    uint8_t new_preset = s_reverb_current_preset;

    if (tr.fraction < 1.0f && tr.surface->id) {
        const mtexinfo_t *surf_info = cl.bsp->texinfo + (tr.surface->id - 1);
        int16_t id = surf_info->step_id;

        for (size_t i = 0; i < s_reverb_active_environment->num_reverbs; i++) {
            const al_reverb_entry_t *entry = &s_reverb_active_environment->reverbs[i];

            if (!entry->num_materials) {
                new_preset = entry->preset;
                break;
            }

            size_t m = 0;

            for (; m < entry->num_materials; m++)
                if (entry->materials[m].step_id == id) {
                    new_preset = entry->preset;
                    break;
                }

            if (m != entry->num_materials)
                break;
        }
    } else {
        new_preset = 19;
    }

    if (new_preset != s_reverb_current_preset) {
        s_reverb_current_preset = new_preset;

        if (s_reverb_lerp_time) {
            memcpy(&s_active_reverb, &s_reverb_lerp_result, sizeof(s_reverb_lerp_result));
        }

        s_reverb_lerp_start = cl.time;
        s_reverb_lerp_time = cl.time + (al_reverb_lerp_time->value * 1000);
        memcpy(&s_reverb_lerp_to, &s_reverb_parameters[s_reverb_current_preset], sizeof(s_active_reverb));
    }

    if (s_reverb_lerp_time) {
        if (cl.time >= s_reverb_lerp_time) {
            s_reverb_lerp_time = 0;
            memcpy(&s_active_reverb, &s_reverb_lerp_to, sizeof(s_active_reverb));
            AL_LoadEffect(&s_active_reverb);
        } else {
            float f = AL_CalculateReverbFrac();

#define AL_LERP(prop) \
                s_reverb_lerp_result.prop = FASTLERP(s_active_reverb.prop, s_reverb_lerp_to.prop, f)
            
            AL_LERP(flDensity);
            AL_LERP(flDiffusion);
            AL_LERP(flGain);
            AL_LERP(flGainHF);
            AL_LERP(flGainLF);
            AL_LERP(flDecayTime);
            AL_LERP(flDecayHFRatio);
            AL_LERP(flDecayLFRatio);
            AL_LERP(flReflectionsGain);
            AL_LERP(flReflectionsDelay);
            AL_LERP(flReflectionsPan[0]);
            AL_LERP(flReflectionsPan[1]);
            AL_LERP(flReflectionsPan[2]);
            AL_LERP(flLateReverbGain);
            AL_LERP(flLateReverbDelay);
            AL_LERP(flLateReverbPan[0]);
            AL_LERP(flLateReverbPan[1]);
            AL_LERP(flLateReverbPan[2]);
            AL_LERP(flEchoTime);
            AL_LERP(flEchoDepth);
            AL_LERP(flModulationTime);
            AL_LERP(flModulationDepth);
            AL_LERP(flAirAbsorptionGainHF);
            AL_LERP(flHFReference);
            AL_LERP(flLFReference);
            AL_LERP(flRoomRolloffFactor);
            AL_LERP(iDecayHFLimit);

            AL_LoadEffect(&s_reverb_lerp_result);
        }
    }
}

static void AL_LoadReverbEntry(json_parse_t *parser, al_reverb_entry_t *out_entry)
{
    size_t fields = parser->pos->size;
    Json_EnsureNext(parser, JSMN_OBJECT);

    for (size_t i = 0; i < fields; i++) {
        if (!Json_Strcmp(parser, "materials")) {
            parser->pos++;

            if (parser->pos->type == JSMN_STRING) {
                if (parser->buffer[parser->pos->start] != '*')
                    Json_Error(parser, parser->pos, "expected string to start with *\n");

                parser->pos++;
            } else {
                size_t n = parser->pos->size;
                Json_EnsureNext(parser, JSMN_ARRAY);
                out_entry->num_materials = n;
                out_entry->materials = Z_TagMalloc(sizeof(*out_entry->materials) * n, TAG_SOUND);

                for (size_t m = 0; m < n; m++, parser->pos++) {
                    Json_Ensure(parser, JSMN_STRING);
                    Q_strnlcpy(out_entry->materials[m].material,
                        parser->buffer + parser->pos->start,
                        parser->pos->end - parser->pos->start,
                        sizeof(out_entry->materials[m].material));
                }
            }

        } else if (!Json_Strcmp(parser, "preset")) {
            parser->pos++;

            Json_Ensure(parser, JSMN_STRING);
            size_t p = 0;

            for (; p < q_countof(s_reverb_names); p++)
                if (!Json_Strcmp(parser, s_reverb_names[p]))
                    break;

            if (p == q_countof(s_reverb_names)) {
                Com_WPrintf("missing sound environment preset\n");
                out_entry->preset = 19; // plain
            } else {
                out_entry->preset = p;
            }

            parser->pos++;
        } else {
            parser->pos++;
            Json_SkipToken(parser);
        }
    }
}

static void AL_LoadReverbEnvironment(json_parse_t *parser, al_reverb_environment_t *out_environment)
{
    size_t fields = parser->pos->size;
    Json_EnsureNext(parser, JSMN_OBJECT);

    for (size_t i = 0; i < fields; i++) {
        if (!Json_Strcmp(parser, "dimension")) {
            Json_Next(parser);
            Json_Ensure(parser, JSMN_PRIMITIVE);
            out_environment->dimension = atof(parser->buffer + parser->pos->start);
            parser->pos++;
        } else if (!Json_Strcmp(parser, "reverbs")) {
            Json_Next(parser);

            out_environment->num_reverbs = parser->pos->size;
            Json_EnsureNext(parser, JSMN_ARRAY);
            out_environment->reverbs = Z_TagMallocz(sizeof(al_reverb_entry_t) * out_environment->num_reverbs, TAG_SOUND);

            for (size_t r = 0; r < out_environment->num_reverbs; r++)
                AL_LoadReverbEntry(parser, &out_environment->reverbs[r]);
        } else {
            parser->pos++;
            Json_SkipToken(parser);
        }
    }
}

static void AL_FreeReverbEnvironments(al_reverb_environment_t *environments, size_t num_environments)
{
    for (size_t i = 0; i < num_environments; i++) {
        for (size_t n = 0; n < environments[i].num_reverbs; n++) {
            Z_Free(environments[i].reverbs[n].materials);
        }

        Z_Free(environments[i].reverbs);
    }

    Z_Free(environments);
}

static int16_t AL_FindStepID(const char *material)
{
    if (!strcmp(material, "") || !strcmp(material, "default"))
        return FOOTSTEP_ID_DEFAULT;
    else if (!strcmp(material, "ladder"))
        return FOOTSTEP_ID_LADDER;

    mtexinfo_t *out;
    int i;

    // FIXME: can speed this up later with a hash map of some sort
    for (i = 0, out = cl.bsp->texinfo; i < cl.bsp->numtexinfo; i++, out++) {
        if (!strcmp(out->c.material, material)) {
            return out->step_id;
        }
    }

    return FOOTSTEP_ID_DEFAULT;
}

static void AL_SetReverbStepIDs(void)
{
    for (size_t i = 0; i < s_num_reverb_environments; i++) {
        for (size_t n = 0; n < s_reverb_environments[i].num_reverbs; n++) {
            al_reverb_entry_t *entry = &s_reverb_environments[i].reverbs[n];

            for (size_t e = 0; e < entry->num_materials; e++) {
                entry->materials[e].step_id = AL_FindStepID(entry->materials[e].material);
            }
        }
    }
}

static void AL_LoadReverbEnvironments(void)
{
    json_parse_t parser = {0};
    al_reverb_environment_t *environments = NULL;
    size_t n = 0;

    if (Json_ErrorHandler(parser)) {
        Com_WPrintf("Couldn't load sound/default.environments[%s]; %s\n", parser.error_loc, parser.error);
        AL_FreeReverbEnvironments(environments, n);
        return;
    }

    Json_Load("sound/default.environments", &parser);

    Json_EnsureNext(&parser, JSMN_OBJECT);

    if (Json_Strcmp(&parser, "environments")) 
        Json_Error(&parser, parser.pos, "expected \"environments\" key\n");

    Json_Next(&parser);

    n = parser.pos->size;
    if (n == 0) {
        s_reverb_environments = NULL;
        s_num_reverb_environments = 0;
        Json_Free(&parser);
        return;
    }
    Json_EnsureNext(&parser, JSMN_ARRAY);

    environments = Z_TagMallocz(sizeof(al_reverb_environment_t) * n, TAG_SOUND);

    for (size_t i = 0; i < n; i++)
        AL_LoadReverbEnvironment(&parser, &environments[i]);

    s_reverb_environments = environments;
    s_num_reverb_environments = n;

    Json_Free(&parser);
}

static void AL_Reverb_stat(void)
{
    SCR_StatKeyValue("dimensions", va("%g", s_reverb_probe_avg));
    SCR_StatKeyValue("env dim", va("%g", s_reverb_active_environment->dimension));
    SCR_StatKeyValue("preset", s_reverb_names[s_reverb_current_preset]);

#define AL_STATF(e) SCR_StatKeyValue(#e, va("%g", s_reverb_lerp_result.e))
#define AL_STATI(e) SCR_StatKeyValue(#e, va("%d", s_reverb_lerp_result.e))
    
    AL_STATF(flDensity);
    AL_STATF(flDiffusion);
    AL_STATF(flGain);
    AL_STATF(flGainHF);
    AL_STATF(flGainLF);
    AL_STATF(flDecayTime);
    AL_STATF(flDecayHFRatio);
    AL_STATF(flDecayLFRatio);
    AL_STATF(flReflectionsGain);
    AL_STATF(flReflectionsDelay);
    AL_STATF(flLateReverbGain);
    AL_STATF(flLateReverbDelay);
    AL_STATF(flEchoTime);
    AL_STATF(flEchoDepth);
    AL_STATF(flModulationTime);
    AL_STATF(flModulationDepth);
    AL_STATF(flAirAbsorptionGainHF);
    AL_STATF(flHFReference);
    AL_STATF(flLFReference);
    AL_STATF(flRoomRolloffFactor);
    AL_STATI(iDecayHFLimit);

    SCR_StatKeyValue("lerp", !s_reverb_lerp_time ? "none" : va("%g", AL_CalculateReverbFrac()));
}

static void AL_StreamStop(void);

static void AL_SoundInfo(void)
{
    Com_Printf("AL_VENDOR: %s\n", qalGetString(AL_VENDOR));
    Com_Printf("AL_RENDERER: %s\n", qalGetString(AL_RENDERER));
    Com_Printf("AL_VERSION: %s\n", qalGetString(AL_VERSION));
    Com_Printf("AL_EXTENSIONS: %s\n", qalGetString(AL_EXTENSIONS));
    Com_Printf("Number of sources: %d\n", s_numchannels);
}

static void s_underwater_gain_hf_changed(cvar_t *self)
{
    if (s_underwater_flag) {
        for (int i = 0; i < s_numchannels; i++)
            qalSourcei(s_srcnums[i], AL_DIRECT_FILTER, 0);
        s_underwater_flag = false;
    }

    qalFilterf(s_underwater_filter, AL_LOWPASS_GAINHF, Cvar_ClampValue(self, 0.001f, 1));
}

static void al_reverb_changed(cvar_t *self)
{
    S_StopAllSounds();
}

static bool AL_Init(void)
{
    int i;

    Com_DPrintf("Initializing OpenAL\n");

    if (!QAL_Init()) {
        goto fail0;
    }

    Com_DPrintf("AL_VENDOR: %s\n", qalGetString(AL_VENDOR));
    Com_DPrintf("AL_RENDERER: %s\n", qalGetString(AL_RENDERER));
    Com_DPrintf("AL_VERSION: %s\n", qalGetString(AL_VERSION));
    Com_DDPrintf("AL_EXTENSIONS: %s\n", qalGetString(AL_EXTENSIONS));

    // check for linear distance extension
    if (!qalIsExtensionPresent("AL_EXT_LINEAR_DISTANCE")) {
        Com_SetLastError("AL_EXT_LINEAR_DISTANCE extension is missing");
        goto fail1;
    }

    // generate source names
    qalGetError();
    qalGenSources(1, &s_stream);

    s_srcnums = Z_TagMalloc(sizeof(*s_srcnums) * s_maxchannels, TAG_SOUND);

    for (i = 0; i < s_maxchannels; i++) {
        qalGenSources(1, &s_srcnums[i]);
        if (qalGetError() != AL_NO_ERROR) {
            break;
        }
        s_numalsources++;
    }

    if (s_numalsources != s_maxchannels)
        s_srcnums = Z_Realloc(s_srcnums, sizeof(*s_srcnums) * s_numalsources);

    Com_DPrintf("Got %d AL sources\n", i);

    if (i < MIN_CHANNELS) {
        Com_SetLastError("Insufficient number of AL sources");
        goto fail1;
    }

    s_numchannels = i;

    s_loop_points = qalIsExtensionPresent("AL_SOFT_loop_points");
    s_source_spatialize = qalIsExtensionPresent("AL_SOFT_source_spatialize");

    // init stream source
    qalSourcef(s_stream, AL_ROLLOFF_FACTOR, 0.0f);
    qalSourcei(s_stream, AL_SOURCE_RELATIVE, AL_TRUE);
    if (s_source_spatialize)
        qalSourcei(s_stream, AL_SOURCE_SPATIALIZE_SOFT, AL_FALSE);

    if (qalIsExtensionPresent("AL_SOFT_direct_channels_remix"))
        qalSourcei(s_stream, AL_DIRECT_CHANNELS_SOFT, AL_REMIX_UNMATCHED_SOFT);
    else if (qalIsExtensionPresent("AL_SOFT_direct_channels"))
        qalSourcei(s_stream, AL_DIRECT_CHANNELS_SOFT, AL_TRUE);

    // init underwater filter
    if (qalGenFilters && qalGetEnumValue("AL_FILTER_LOWPASS")) {
        qalGenFilters(1, &s_underwater_filter);
        qalFilteri(s_underwater_filter, AL_FILTER_TYPE, AL_FILTER_LOWPASS);
        s_underwater_gain_hf->changed = s_underwater_gain_hf_changed;
        s_underwater_gain_hf_changed(s_underwater_gain_hf);
    }

    if (qalGenEffects && qalGetEnumValue("AL_EFFECT_EAXREVERB")) {
        qalGenEffects(1, &s_reverb_effect);
        qalGenAuxiliaryEffectSlots(1, &s_reverb_slot);
        qalEffecti(s_reverb_effect, AL_EFFECT_TYPE, AL_EFFECT_EAXREVERB);
    }

    al_reverb = Cvar_Get("al_reverb", "1", 0);
    al_reverb->changed = al_reverb_changed;
    al_reverb_lerp_time = Cvar_Get("al_reverb_lerp_time", "3.0", 0);

    al_timescale = Cvar_Get("al_timescale", "1", 0);
    al_merge_looping = Cvar_Get("al_merge_looping", "1", 0);

    SCR_RegisterStat("al_reverb", AL_Reverb_stat);

    Com_Printf("OpenAL initialized.\n");
    return true;

fail1:
    QAL_Shutdown();
fail0:
    Com_EPrintf("Failed to initialize OpenAL: %s\n", Com_GetLastError());
    return false;
}

static void AL_Shutdown(void)
{
    Com_Printf("Shutting down OpenAL.\n");

    if (s_numchannels) {
        // delete source names
        qalDeleteSources(s_numchannels, s_srcnums);
        Z_Free(s_srcnums);
        s_srcnums = NULL;
        s_numalsources = 0;
        s_numchannels = 0;
    }

    if (s_stream) {
        AL_StreamStop();
        qalDeleteSources(1, &s_stream);
        s_stream = 0;
    }

    if (s_underwater_filter) {
        qalDeleteFilters(1, &s_underwater_filter);
        s_underwater_filter = 0;
    }

    if (s_reverb_effect) {
        qalDeleteEffects(1, &s_reverb_effect);
        s_reverb_effect = 0;
    }

    if (s_reverb_slot) {
        qalDeleteAuxiliaryEffectSlots(1, &s_reverb_slot);
        s_reverb_slot = 0;
    }

    AL_FreeReverbEnvironments(s_reverb_environments, s_num_reverb_environments);
    s_reverb_environments = NULL;
    s_num_reverb_environments = 0;

    s_underwater_flag = false;
    s_underwater_gain_hf->changed = NULL;

    SCR_UnregisterStat("al_reverb");

    QAL_Shutdown();
}

static sfxcache_t *AL_UploadSfx(sfx_t *s)
{
    byte *converted_data = NULL;
    int sample_width = s_info.width;
     if (s_info.width == 3) {
        /* 24-bit sounds: Sample down to 16-bit.
         * Alternatively, could use AL_EXT_float32 and upload as float. */
        size_t numsamples = s_info.samples * s_info.channels;
        converted_data = Z_Malloc(numsamples * sizeof(uint16_t));
        const byte *input_ptr = s_info.data;
        uint16_t *output_ptr = (uint16_t *)converted_data;
        for (size_t i = 0; i < numsamples; i++) {
            *output_ptr = input_ptr[1] | (input_ptr[2] << 8);
            output_ptr++;
            input_ptr += 3;
        }
        sample_width = 2;
     }

    ALsizei size = s_info.samples * sample_width * s_info.channels;
    ALenum format = AL_FORMAT_MONO8 + (s_info.channels - 1) * 2 + (sample_width - 1);
    ALuint buffer = 0;

    qalGetError();
    qalGenBuffers(1, &buffer);
    if (qalGetError()) {
        Com_SetLastError("Failed to generate buffer");
        goto fail;
    }

    qalBufferData(buffer, format, converted_data ? converted_data : s_info.data, size, s_info.rate);
    if (qalGetError()) {
        Com_SetLastError("Failed to upload samples");
        qalDeleteBuffers(1, &buffer);
        goto fail;
    }

    // specify OpenAL-Soft style loop points
    if (s_info.loopstart > 0 && s_loop_points) {
        ALint points[2] = { s_info.loopstart, s_info.samples };
        qalBufferiv(buffer, AL_LOOP_POINTS_SOFT, points);
    }

    // allocate placeholder sfxcache
    sfxcache_t *sc = s->cache = S_Malloc(sizeof(*sc));
    sc->length = s_info.samples * 1000LL / s_info.rate; // in msec
    sc->loopstart = s_info.loopstart;
    sc->width = sample_width;
    sc->channels = s_info.channels;
    sc->size = size;
    sc->bufnum = buffer;

    return sc;

fail:
    Z_Free(converted_data);
    s->error = Q_ERR_LIBRARY_ERROR;
    return NULL;
}

static void AL_DeleteSfx(sfx_t *s)
{
    sfxcache_t *sc = s->cache;
    if (sc) {
        ALuint name = sc->bufnum;
        qalDeleteBuffers(1, &name);
    }
}

static int AL_GetBeginofs(float timeofs)
{
    return s_paintedtime + timeofs * 1000;
}

static void AL_Spatialize(channel_t *ch)
{
    vec3_t      origin;

    // anything coming from the view entity will always be full volume
    // no attenuation = no spatialization
    if (S_IsFullVolume(ch)) {
        VectorCopy(listener_origin, origin);
    } else if (ch->fixed_origin) {
        VectorCopy(ch->origin, origin);
    } else {
        CL_GetEntitySoundOrigin(ch->entnum, origin);
    }

    if (s_source_spatialize) {
        qalSourcei(ch->srcnum, AL_SOURCE_SPATIALIZE_SOFT, !S_IsFullVolume(ch));
    }

    qalSource3f(ch->srcnum, AL_POSITION, AL_UnpackVector(origin));

    if (al_timescale->integer) {
        qalSourcef(ch->srcnum, AL_PITCH, max(0.75f, CL_Wheel_TimeScale() * Cvar_VariableValue("timescale")));
    } else {
        qalSourcef(ch->srcnum, AL_PITCH, 1.0f);
    }
}

static void AL_StopChannel(channel_t *ch)
{
    if (!ch->sfx)
        return;

#if USE_DEBUG
    if (s_show->integer > 1)
        Com_Printf("%s: %s\n", __func__, ch->sfx->name);
#endif

    // stop it
    qalSourceStop(ch->srcnum);
    qalSourcei(ch->srcnum, AL_BUFFER, AL_NONE);
    memset(ch, 0, sizeof(*ch));
}

static void AL_PlayChannel(channel_t *ch)
{
    sfxcache_t *sc = ch->sfx->cache;

#if USE_DEBUG
    if (s_show->integer > 1)
        Com_Printf("%s: %s\n", __func__, ch->sfx->name);
#endif

    ch->srcnum = s_srcnums[ch - s_channels];
    qalGetError();
    qalSourcei(ch->srcnum, AL_BUFFER, sc->bufnum);
    qalSourcei(ch->srcnum, AL_LOOPING, ch->autosound || sc->loopstart >= 0);
    qalSourcef(ch->srcnum, AL_GAIN, ch->master_vol);
    qalSourcef(ch->srcnum, AL_REFERENCE_DISTANCE, SOUND_FULLVOLUME);
    qalSourcef(ch->srcnum, AL_MAX_DISTANCE, 8192);
    qalSourcef(ch->srcnum, AL_ROLLOFF_FACTOR, ch->dist_mult * (8192 - SOUND_FULLVOLUME));

    if (cl.bsp && s_reverb_slot && al_reverb->integer) {
        qalSource3i(ch->srcnum, AL_AUXILIARY_SEND_FILTER, s_reverb_slot, 0, AL_FILTER_NULL);
    } else {
        qalSource3i(ch->srcnum, AL_AUXILIARY_SEND_FILTER, AL_EFFECT_NULL, 0, AL_FILTER_NULL);
    }

    AL_Spatialize(ch);

    // play it
    qalSourcePlay(ch->srcnum);
    if (qalGetError() != AL_NO_ERROR) {
        AL_StopChannel(ch);
    } else {
        if (ch->autosound) {
            qalSourcef(ch->srcnum, AL_SEC_OFFSET, fmodf(cls.realtime / 1000.f, ch->sfx->cache->length / 1000.f));
        }
    }
}

static void AL_IssuePlaysounds(void)
{
    // start any playsounds
    while (1) {
        playsound_t *ps = PS_FIRST(&s_pendingplays);
        if (PS_TERM(ps, &s_pendingplays))
            break;  // no more pending sounds
        if (ps->begin > s_paintedtime)
            break;
        S_IssuePlaysound(ps);
    }
}

static void AL_StopAllSounds(void)
{
    int         i;
    channel_t   *ch;

    for (i = 0, ch = s_channels; i < s_numchannels; i++, ch++) {
        if (!ch->sfx)
            continue;
        AL_StopChannel(ch);
    }
}

static channel_t *AL_FindLoopingSound(int entnum, const sfx_t *sfx)
{
    int         i;
    channel_t   *ch;

    for (i = 0, ch = s_channels; i < s_numchannels; i++, ch++) {
        if (!ch->autosound)
            continue;
        if (entnum && ch->entnum != entnum)
            continue;
        if (ch->sfx != sfx)
            continue;
        return ch;
    }

    return NULL;
}

static int al_loop_sounds[MAX_EDICTS];

/*
 * Paril: the existing AL driver handled looping sounds wrong (below)
 * and simply added them to the world as-is. This lost the behavior in
 * vanilla where looping noises were technically a single merged source
 * and makes them way louder than they should be by causing them to
 * stack.
 *
 * This is my attempt to fix this behavior by doing something
 * closer to vanilla Q2.
 * 
 * TODO: this currently always uses the position of the entity
 * that is best suited for the looping noise; this causes the
 * audio to shift left/right if you go between two grenades for instance.
 * In theory, we could shift the real position of the audio source to
 * let AL then spatialize it better or something.
 */
static void AL_AddMergeLoopSounds(void)
{
    int         i;
    channel_t   *ch;
    sfx_t       *sfx;
    sfxcache_t  *sc;

    if (cls.state != ca_active || sv_paused->integer || !s_ambient->integer)
        return;

    S_BuildSoundList(al_loop_sounds);

    // for each unique sound, poll each source nearby
    // and see which one contributes the most.
    for (i = 0; i < cl.frame.numEntities; i++) {
        if (!al_loop_sounds[i])
            continue;

        sfx = S_SfxForHandle(cl.sound_precache[al_loop_sounds[i]]);
        if (!sfx)
            continue;       // bad sound effect
        sc = sfx->cache;
        if (!sc)
            continue;

        // find a channel
        bool is_new = false;

        ch = AL_FindLoopingSound(0, sfx);
        if (!ch) {
            ch = S_PickChannel(0, 0);
            if (!ch)
                continue;

            // allocate a channel
            ch->autosound = true;   // remove next frame
            ch->sfx = sfx;
            is_new = true;
        }

        // find the entity that would contribute the most
        entity_state_t *best = NULL;
        float best_contrib = INFINITY;

        // this is just set so S_IsFullVolume below works
        ch->dist_mult = 1.0f;

        for (int n = 0; n < cl.frame.numEntities; n++) {
            if (al_loop_sounds[n] != al_loop_sounds[i]) {
                continue;
            }
            
            int num2 = (cl.frame.firstEntity + n) & PARSE_ENTITIES_MASK;
            entity_state_t *ent2 = &cl.entityStates[num2];
            
            float dist_mult = S_GetEntityLoopDistMult(ent2);
            float master_vol = S_GetEntityLoopVolume(ent2);

            if (!dist_mult || ent2->number == listener_entnum) {
                // always full volume
                ch->master_vol = master_vol;
                ch->dist_mult = dist_mult;
                best = ent2;
                break;
            }

            // calculate stereo separation and distance attenuation
            vec3_t source_vec;
            CL_GetEntitySoundOrigin(ent2->number, source_vec);
            VectorSubtract(source_vec, listener_origin, source_vec);

            float dist = VectorNormalize(source_vec);
            dist -= SOUND_FULLVOLUME;
            if (dist < 0)
                dist = 0;           // close enough to be at full volume
            dist *= dist_mult;      // different attenuation levels

            float contrib = master_vol * dist;

            if (!best || contrib < best_contrib) {
                ch->master_vol = master_vol;
                ch->dist_mult = dist_mult;
                best = ent2;
                best_contrib = contrib;
            }
        }

        ch->entnum = best->number;

        ch->autoframe = s_framecount;
        ch->end = s_paintedtime + sc->length;

        if (is_new) {
            AL_PlayChannel(ch);
        } else {
            qalSourcef(ch->srcnum, AL_GAIN, ch->master_vol);
            qalSourcef(ch->srcnum, AL_ROLLOFF_FACTOR, ch->dist_mult * (8192 - SOUND_FULLVOLUME));
        }
    }
}

static void AL_AddLoopSounds(void)
{
    int         i;
    channel_t   *ch;
    sfx_t       *sfx;
    sfxcache_t  *sc;
    int         num;
    entity_state_t *ent;

    if (cls.state != ca_active || sv_paused->integer || !s_ambient->integer)
        return;

    S_BuildSoundList(al_loop_sounds);

    for (i = 0; i < cl.frame.numEntities; i++) {
        if (!al_loop_sounds[i])
            continue;

        sfx = S_SfxForHandle(cl.sound_precache[al_loop_sounds[i]]);
        if (!sfx)
            continue;       // bad sound effect
        sc = sfx->cache;
        if (!sc)
            continue;

        num = (cl.frame.firstEntity + i) & PARSE_ENTITIES_MASK;
        ent = &cl.entityStates[num];

        ch = AL_FindLoopingSound(ent->number, sfx);
        if (ch) {
            ch->autoframe = s_framecount;
            ch->end = s_paintedtime + sc->length;
            continue;
        }

        // allocate a channel
        ch = S_PickChannel(0, 0);
        if (!ch)
            continue;

        ch->autosound = true;   // remove next frame
        ch->autoframe = s_framecount;
        ch->sfx = sfx;
        ch->entnum = ent->number;
        ch->master_vol = S_GetEntityLoopVolume(ent);
        ch->dist_mult = S_GetEntityLoopDistMult(ent);
        ch->end = s_paintedtime + sc->length;

        AL_PlayChannel(ch);
    }
}

static void AL_StreamUpdate(void)
{
    ALint num_buffers = 0;
    qalGetSourcei(s_stream, AL_BUFFERS_PROCESSED, &num_buffers);
    while (num_buffers-- > 0) {
        ALuint buffer = 0;
        qalSourceUnqueueBuffers(s_stream, 1, &buffer);
        qalDeleteBuffers(1, &buffer);
        s_stream_buffers--;
    }
}

static void AL_StreamStop(void)
{
    qalSourceStop(s_stream);
    AL_StreamUpdate();
    Q_assert(!s_stream_buffers);
}

static int AL_NeedRawSamples(void)
{
    return s_stream_buffers < 32 ? MAX_RAW_SAMPLES : 0;
}

static bool AL_RawSamples(int samples, int rate, int width, int channels, const byte *data, float volume)
{
    ALenum format = AL_FORMAT_MONO8 + (channels - 1) * 2 + (width - 1);
    ALuint buffer = 0;

    if (AL_NeedRawSamples()) {
        qalGetError();
        qalGenBuffers(1, &buffer);
        if (qalGetError())
            return false;

        qalBufferData(buffer, format, data, samples * width * channels, rate);
        if (qalGetError()) {
            qalDeleteBuffers(1, &buffer);
            return false;
        }

        qalSourceQueueBuffers(s_stream, 1, &buffer);
        if (qalGetError()) {
            qalDeleteBuffers(1, &buffer);
            return false;
        }
        s_stream_buffers++;
    }

    qalSourcef(s_stream, AL_GAIN, volume);

    ALint state = AL_PLAYING;
    qalGetSourcei(s_stream, AL_SOURCE_STATE, &state);
    if (state != AL_PLAYING)
        qalSourcePlay(s_stream);
    return true;
}

static void AL_UpdateUnderWater(void)
{
    bool underwater = S_IsUnderWater();
    ALint filter = 0;

    if (!s_underwater_filter)
        return;

    if (s_underwater_flag == underwater)
        return;

    if (underwater)
        filter = s_underwater_filter;

    for (int i = 0; i < s_numchannels; i++)
        qalSourcei(s_srcnums[i], AL_DIRECT_FILTER, filter);

    s_underwater_flag = underwater;
}

static void AL_Update(void)
{
    int         i;
    channel_t   *ch;
    ALfloat     orientation[6];

    if (!s_active)
        return;

    s_paintedtime = cl.time;

    // set listener parameters
    qalListener3f(AL_POSITION, AL_UnpackVector(listener_origin));
    AL_CopyVector(listener_forward, orientation);
    AL_CopyVector(listener_up, orientation + 3);
    qalListenerfv(AL_ORIENTATION, orientation);
    qalListenerf(AL_GAIN, s_volume->value);
    qalDistanceModel(AL_LINEAR_DISTANCE_CLAMPED);

    AL_UpdateUnderWater();
    
    if (al_reverb->integer) {
        AL_UpdateReverb();
    }

    // update spatialization for dynamic sounds
    for (i = 0, ch = s_channels; i < s_numchannels; i++, ch++) {
        if (!ch->sfx)
            continue;

        if (ch->autosound) {
            // autosounds are regenerated fresh each frame
            if (ch->autoframe != s_framecount) {
                AL_StopChannel(ch);
                continue;
            }
        } else {
            ALenum state = AL_STOPPED;
            qalGetSourcei(ch->srcnum, AL_SOURCE_STATE, &state);
            if (state == AL_STOPPED) {
                AL_StopChannel(ch);
                continue;
            }
        }

#if USE_DEBUG
        if (s_show->integer) {
            ALfloat offset = 0;
            qalGetSourcef(ch->srcnum, AL_SEC_OFFSET, &offset);
            Com_Printf("%d %.1f %.1f %s\n", i, ch->master_vol, offset, ch->sfx->name);
        }
#endif

        AL_Spatialize(ch);         // respatialize channel
    }

    s_framecount++;

    // add loopsounds
    if (al_merge_looping->integer) {
        AL_AddMergeLoopSounds();
    } else {
        AL_AddLoopSounds();
    }

    AL_IssuePlaysounds();

    AL_StreamUpdate();
}

static void AL_EndRegistration(void)
{
    AL_FreeReverbEnvironments(s_reverb_environments, s_num_reverb_environments);
    s_reverb_environments = NULL;
    s_num_reverb_environments = 0;

    AL_LoadReverbEnvironments();

    if (!s_reverb_environments)
        return;

    s_reverb_current_preset = 19;
    memcpy(&s_active_reverb, &s_reverb_parameters[s_reverb_current_preset], sizeof(s_active_reverb));
    AL_LoadEffect(&s_active_reverb);
    s_reverb_lerp_start = s_reverb_lerp_time = 0;

    s_reverb_probe_time = 0;
    s_reverb_probe_index = 0;
    for (int i = 0; i < q_countof(s_reverb_probes); i++)
        VectorClear(s_reverb_probe_results[i]);
    s_reverb_probe_avg = (float) 8192;
    s_reverb_active_environment = &s_reverb_environments[s_num_reverb_environments - 1];

    if (cl.bsp)
        AL_SetReverbStepIDs();
}

const sndapi_t snd_openal = {
    .init = AL_Init,
    .shutdown = AL_Shutdown,
    .update = AL_Update,
    .activate = S_StopAllSounds,
    .sound_info = AL_SoundInfo,
    .upload_sfx = AL_UploadSfx,
    .delete_sfx = AL_DeleteSfx,
    .raw_samples = AL_RawSamples,
    .need_raw_samples = AL_NeedRawSamples,
    .drop_raw_samples = AL_StreamStop,
    .get_begin_ofs = AL_GetBeginofs,
    .play_channel = AL_PlayChannel,
    .stop_channel = AL_StopChannel,
    .stop_all_sounds = AL_StopAllSounds,
    .end_registration = AL_EndRegistration,
};
