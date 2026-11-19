#include "stdafx.h"
#include <windows.h>

// Dynamic Miles Sound System (mss64.dll) loader with safe no-op stubs when absent.
static HMODULE g_hMilesDll = NULL;
static bool g_milesBindAttempted = false;

static void AILCALL Stub_AIL_close_digital_driver(HDIGDRIVER dig) { (void)dig; }
static void AILCALL Stub_AIL_close_stream(HSTREAM stream) { (void)stream; }
static S32 AILCALL Stub_AIL_enqueue_event_buffer(S32* token, void* user_buffer, S32 user_buffer_len, S32 user_buffer_is_ptr) { (void)token; (void)user_buffer; (void)user_buffer_len; (void)user_buffer_is_ptr; return 0; }
static U64 AILCALL Stub_AIL_enqueue_event_end_named(S32 token, char const* event_name) { (void)token; (void)event_name; return 1; }
static S32 AILCALL Stub_AIL_enqueue_event_start() { return 1; }
static S32 AILCALL Stub_AIL_enumerate_events(HMSOUNDBANK bank, HMSSENUM* next, char const* list, char const** name) { (void)bank; (void)next; (void)list; (void)name; return 0; }
static char* AILCALL Stub_AIL_last_error(void) { static char err[] = ""; return err; }
static HDIGDRIVER AILCALL Stub_AIL_open_digital_driver(U32 frequency, S32 bits, S32 channel, U32 flags) { (void)frequency; (void)bits; (void)channel; (void)flags; return (HDIGDRIVER)0; }
static HSTREAM AILCALL Stub_AIL_open_stream(HDIGDRIVER dig, char const* filename, S32 stream_mem) { (void)dig; (void)filename; (void)stream_mem; return (HSTREAM)0; }
static void AILCALL Stub_AIL_pause_stream(HSTREAM stream, S32 onoff) { (void)stream; (void)onoff; }
static AILFALLOFFCB AILCALL Stub_AIL_register_falloff_function_callback(HSAMPLE S, AILFALLOFFCB falloff_cb) { (void)S; (void)falloff_cb; return NULL; }
static void AILCALL Stub_AIL_set_3D_rolloff_factor(HDIGDRIVER dig, F32 factor) { (void)dig; (void)factor; }
static void AILCALL Stub_AIL_set_listener_3D_orientation(HDIGDRIVER dig, F32 X_face, F32 Y_face, F32 Z_face, F32 X_up, F32 Y_up, F32 Z_up) { (void)dig; (void)X_face; (void)Y_face; (void)Z_face; (void)X_up; (void)Y_up; (void)Z_up; }
static void AILCALL Stub_AIL_set_listener_3D_position(HDIGDRIVER dig, F32 X, F32 Y, F32 Z) { (void)dig; (void)X; (void)Y; (void)Z; }
static void AILCALL Stub_AIL_set_sample_3D_distances(HSAMPLE S, F32 max_dist, F32 min_dist, S32 auto_3D_wet_atten) { (void)S; (void)max_dist; (void)min_dist; (void)auto_3D_wet_atten; }
static void AILCALL Stub_AIL_set_sample_3D_position(HSAMPLE obj, F32 X, F32 Y, F32 Z) { (void)obj; (void)X; (void)Y; (void)Z; }
static S32 AILEXPORT Stub_AIL_set_sample_is_3D(HSAMPLE S, S32 onoff) { (void)S; (void)onoff; return 0; }
static void AILCALL Stub_AIL_set_sample_playback_rate_factor(HSAMPLE S, F32 playback_rate_factor) { (void)S; (void)playback_rate_factor; }
static void AILCALL Stub_AIL_set_sample_volume_levels(HSAMPLE S, F32 left_level, F32 right_level) { (void)S; (void)left_level; (void)right_level; }
static void AILCALL Stub_AIL_shutdown(void) {}
static void AILCALL Stub_AIL_start_stream(HSTREAM stream) { (void)stream; }
static HSAMPLE AILCALL Stub_AIL_stream_sample_handle(HSTREAM stream) { (void)stream; return (HSAMPLE)0; }
static S32 AILCALL Stub_AIL_stream_status(HSTREAM stream) { (void)stream; return 0; }

static HMSOUNDBANK AILCALL Stub_MilesAddSoundBank(char const* filename, char const* name) { (void)filename; (void)name; return (HMSOUNDBANK)0; }
static S32 AILCALL Stub_MilesBeginEventQueueProcessing(void) { return 0; }
static S32 AILCALL Stub_MilesCompleteEventQueueProcessing(void) { return 0; }
static U64 AILCALL Stub_MilesEnqueueEventByName(char const* name) { (void)name; return 0; }
static S32 AILCALL Stub_MilesEnumerateSoundInstances(HEVENTSYSTEM system, HMSSENUM* next, S32 statuses, char const* label_query, U64 search_for_ID, EXPOUT MILESEVENTSOUNDINFO* info) { (void)system; (void)next; (void)statuses; (void)label_query; (void)search_for_ID; (void)info; return 0; }
static void AILCALL Stub_MilesSetEventErrorCallback(AILEVENTERRORCB* i_ErrorCallback) { (void)i_ErrorCallback; }
static HEVENTSYSTEM AILCALL Stub_MilesStartupEventSystem(HDIGDRIVER dig, S32 command_buf_len, EXPOUT char* memory_buf, S32 memory_len) { (void)dig; (void)command_buf_len; (void)memory_buf; (void)memory_len; return (HEVENTSYSTEM)0; }

static char* AILCALL Hook_AIL_set_redist_directory(char const* dir);
static S32 AILCALL Hook_AIL_startup(void);

extern "C" {
    void* __imp_AIL_close_digital_driver = (void*)&Stub_AIL_close_digital_driver;
    void* __imp_AIL_close_stream = (void*)&Stub_AIL_close_stream;
    void* __imp_AIL_enqueue_event_buffer = (void*)&Stub_AIL_enqueue_event_buffer;
    void* __imp_AIL_enqueue_event_end_named = (void*)&Stub_AIL_enqueue_event_end_named;
    void* __imp_AIL_enqueue_event_start = (void*)&Stub_AIL_enqueue_event_start;
    void* __imp_AIL_enumerate_events = (void*)&Stub_AIL_enumerate_events;
    void* __imp_AIL_last_error = (void*)&Stub_AIL_last_error;
    void* __imp_AIL_open_digital_driver = (void*)&Stub_AIL_open_digital_driver;
    void* __imp_AIL_open_stream = (void*)&Stub_AIL_open_stream;
    void* __imp_AIL_pause_stream = (void*)&Stub_AIL_pause_stream;
    void* __imp_AIL_register_falloff_function_callback = (void*)&Stub_AIL_register_falloff_function_callback;
    void* __imp_AIL_set_3D_rolloff_factor = (void*)&Stub_AIL_set_3D_rolloff_factor;
    void* __imp_AIL_set_listener_3D_orientation = (void*)&Stub_AIL_set_listener_3D_orientation;
    void* __imp_AIL_set_listener_3D_position = (void*)&Stub_AIL_set_listener_3D_position;
    void* __imp_AIL_set_redist_directory = (void*)&Hook_AIL_set_redist_directory;
    void* __imp_AIL_set_sample_3D_distances = (void*)&Stub_AIL_set_sample_3D_distances;
    void* __imp_AIL_set_sample_3D_position = (void*)&Stub_AIL_set_sample_3D_position;
    void* __imp_AIL_set_sample_is_3D = (void*)&Stub_AIL_set_sample_is_3D;
    void* __imp_AIL_set_sample_playback_rate_factor = (void*)&Stub_AIL_set_sample_playback_rate_factor;
    void* __imp_AIL_set_sample_volume_levels = (void*)&Stub_AIL_set_sample_volume_levels;
    void* __imp_AIL_shutdown = (void*)&Stub_AIL_shutdown;
    void* __imp_AIL_start_stream = (void*)&Stub_AIL_start_stream;
    void* __imp_AIL_startup = (void*)&Hook_AIL_startup;
    void* __imp_AIL_stream_sample_handle = (void*)&Stub_AIL_stream_sample_handle;
    void* __imp_AIL_stream_status = (void*)&Stub_AIL_stream_status;

    void* __imp_MilesAddSoundBank = (void*)&Stub_MilesAddSoundBank;
    void* __imp_MilesBeginEventQueueProcessing = (void*)&Stub_MilesBeginEventQueueProcessing;
    void* __imp_MilesCompleteEventQueueProcessing = (void*)&Stub_MilesCompleteEventQueueProcessing;
    void* __imp_MilesEnqueueEventByName = (void*)&Stub_MilesEnqueueEventByName;
    void* __imp_MilesEnumerateSoundInstances = (void*)&Stub_MilesEnumerateSoundInstances;
    void* __imp_MilesSetEventErrorCallback = (void*)&Stub_MilesSetEventErrorCallback;
    void* __imp_MilesStartupEventSystem = (void*)&Stub_MilesStartupEventSystem;
}

static bool BindMilesExports(const char* preferredDir)
{
    if (g_hMilesDll != NULL)
    {
        return true;
    }

    if (preferredDir != NULL && preferredDir[0] != '\0')
    {
        char candidate[MAX_PATH] = {};
        snprintf(candidate, MAX_PATH - 1, "%s\\mss64.dll", preferredDir);
        g_hMilesDll = LoadLibraryA(candidate);
        if (!g_hMilesDll)
        {
            snprintf(candidate, MAX_PATH - 1, "%smss64.dll", preferredDir);
            g_hMilesDll = LoadLibraryA(candidate);
        }
    }

    if (!g_hMilesDll)
    {
        g_hMilesDll = LoadLibraryA("redist64\\mss64.dll");
    }
    if (!g_hMilesDll)
    {
        g_hMilesDll = LoadLibraryA("mss64.dll");
    }
    g_milesBindAttempted = true;
    if (!g_hMilesDll)
    {
        return false;
    }

#define BIND_MILES(fn) \
    do { \
        FARPROC p = GetProcAddress(g_hMilesDll, #fn); \
        if (p != NULL) __imp_##fn = (void*)p; \
    } while (0)

    BIND_MILES(AIL_close_digital_driver);
    BIND_MILES(AIL_close_stream);
    BIND_MILES(AIL_enqueue_event_buffer);
    BIND_MILES(AIL_enqueue_event_end_named);
    BIND_MILES(AIL_enqueue_event_start);
    BIND_MILES(AIL_enumerate_events);
    BIND_MILES(AIL_last_error);
    BIND_MILES(AIL_open_digital_driver);
    BIND_MILES(AIL_open_stream);
    BIND_MILES(AIL_pause_stream);
    BIND_MILES(AIL_register_falloff_function_callback);
    BIND_MILES(AIL_set_3D_rolloff_factor);
    BIND_MILES(AIL_set_listener_3D_orientation);
    BIND_MILES(AIL_set_listener_3D_position);
    BIND_MILES(AIL_set_redist_directory);
    BIND_MILES(AIL_set_sample_3D_distances);
    BIND_MILES(AIL_set_sample_3D_position);
    BIND_MILES(AIL_set_sample_is_3D);
    BIND_MILES(AIL_set_sample_playback_rate_factor);
    BIND_MILES(AIL_set_sample_volume_levels);
    BIND_MILES(AIL_shutdown);
    BIND_MILES(AIL_start_stream);
    BIND_MILES(AIL_startup);
    BIND_MILES(AIL_stream_sample_handle);
    BIND_MILES(AIL_stream_status);

    BIND_MILES(MilesAddSoundBank);
    BIND_MILES(MilesBeginEventQueueProcessing);
    BIND_MILES(MilesCompleteEventQueueProcessing);
    BIND_MILES(MilesEnqueueEventByName);
    BIND_MILES(MilesEnumerateSoundInstances);
    BIND_MILES(MilesSetEventErrorCallback);
    BIND_MILES(MilesStartupEventSystem);
#undef BIND_MILES

    return true;
}

static char* AILCALL Hook_AIL_set_redist_directory(char const* dir)
{
    if (BindMilesExports(dir) && __imp_AIL_set_redist_directory != (void*)&Hook_AIL_set_redist_directory)
    {
        typedef char* (AILCALL *Fn)(char const*);
        return ((Fn)__imp_AIL_set_redist_directory)(dir);
    }
    return NULL;
}

static S32 AILCALL Hook_AIL_startup(void)
{
    if (!g_milesBindAttempted)
    {
        BindMilesExports(NULL);
    }
    if (g_hMilesDll != NULL && __imp_AIL_startup != (void*)&Hook_AIL_startup)
    {
        typedef S32 (AILCALL *Fn)(void);
        return ((Fn)__imp_AIL_startup)();
    }
    return 0;
}
