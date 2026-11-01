#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32)
#define CLAP_ABI __cdecl
#define CLAP_EXPORT __declspec(dllexport)
#elif defined(__GNUC__) || defined(__clang__)
#define CLAP_ABI
#define CLAP_EXPORT __attribute__((visibility("default")))
#else
#define CLAP_ABI
#define CLAP_EXPORT
#endif

#define CLAP_NAME_SIZE 256
#define CLAP_PATH_SIZE 1024

typedef uint32_t clap_id;
static const clap_id CLAP_INVALID_ID = UINT32_MAX;

typedef struct clap_version {
    uint32_t major;
    uint32_t minor;
    uint32_t revision;
} clap_version_t;

static const clap_version_t CLAP_VERSION = {1, 1, 10};

static inline bool clap_version_is_compatible(const clap_version_t v) {
    return v.major >= 1;
}

typedef struct clap_plugin_entry {
    clap_version_t clap_version;
    bool(CLAP_ABI *init)(const char *plugin_path);
    void(CLAP_ABI *deinit)(void);
    const void *(CLAP_ABI *get_factory)(const char *factory_id);
} clap_plugin_entry_t;

static const char CLAP_PLUGIN_FACTORY_ID[] = "clap.plugin-factory";

typedef struct clap_host {
    clap_version_t clap_version;
    void *host_data;
    const char *name;
    const char *vendor;
    const char *url;
    const char *version;
    const void *(CLAP_ABI *get_extension)(const struct clap_host *host, const char *extension_id);
    void(CLAP_ABI *request_restart)(const struct clap_host *host);
    void(CLAP_ABI *request_process)(const struct clap_host *host);
    void(CLAP_ABI *request_callback)(const struct clap_host *host);
} clap_host_t;

typedef struct clap_plugin_descriptor {
    clap_version_t clap_version;
    const char *id;
    const char *name;
    const char *vendor;
    const char *url;
    const char *manual_url;
    const char *support_url;
    const char *version;
    const char *description;
    const char *const *features;
} clap_plugin_descriptor_t;

// Events
enum {
    CLAP_CORE_EVENT_SPACE_ID = 0
};

enum {
    CLAP_EVENT_IS_LIVE = 1 << 0,
    CLAP_EVENT_DONT_RECORD = 1 << 1
};

enum {
    CLAP_EVENT_NOTE_ON = 0,
    CLAP_EVENT_NOTE_OFF = 1,
    CLAP_EVENT_NOTE_CHOKE = 2,
    CLAP_EVENT_NOTE_END = 3,
    CLAP_EVENT_NOTE_EXPRESSION = 4,
    CLAP_EVENT_PARAM_VALUE = 5,
    CLAP_EVENT_PARAM_MOD = 6,
    CLAP_EVENT_PARAM_GESTURE_BEGIN = 7,
    CLAP_EVENT_PARAM_GESTURE_END = 8,
    CLAP_EVENT_TRANSPORT = 9,
    CLAP_EVENT_MIDI = 10,
    CLAP_EVENT_MIDI_SYSEX = 11,
    CLAP_EVENT_MIDI2 = 12
};

typedef struct clap_event_header {
    uint32_t size;
    uint32_t time;
    uint16_t space_id;
    uint16_t type;
    uint32_t flags;
} clap_event_header_t;

typedef struct clap_event_note {
    clap_event_header_t header;
    int32_t note_id;
    int16_t port_index;
    int16_t channel;
    int16_t key;
    double velocity;
} clap_event_note_t;

typedef struct clap_event_param_value {
    clap_event_header_t header;
    clap_id param_id;
    void *cookie;
    int32_t note_id;
    int16_t port_index;
    int16_t channel;
    int16_t key;
    double value;
} clap_event_param_value_t;

typedef struct clap_event_param_gesture {
    clap_event_header_t header;
    clap_id param_id;
} clap_event_param_gesture_t;

typedef struct clap_event_midi {
    clap_event_header_t header;
    uint16_t port_index;
    uint8_t data[3];
} clap_event_midi_t;

typedef struct clap_input_events {
    void *ctx;
    uint32_t(CLAP_ABI *size)(const struct clap_input_events *list);
    const clap_event_header_t *(CLAP_ABI *get)(const struct clap_input_events *list, uint32_t index);
} clap_input_events_t;

typedef struct clap_output_events {
    void *ctx;
    bool(CLAP_ABI *try_push)(const struct clap_output_events *list, const clap_event_header_t *event);
} clap_output_events_t;

typedef struct clap_audio_buffer {
    float **data32;
    double **data64;
    uint32_t channel_count;
    uint32_t latency;
    uint64_t constant_mask;
} clap_audio_buffer_t;

typedef int32_t clap_process_status;
enum {
    CLAP_PROCESS_ERROR = 0,
    CLAP_PROCESS_CONTINUE = 1,
    CLAP_PROCESS_CONTINUE_IF_NOT_QUIET = 2,
    CLAP_PROCESS_TAIL = 3,
    CLAP_PROCESS_SLEEP = 4
};

typedef struct clap_process {
    int64_t steady_time;
    uint32_t frames_count;
    const void *transport;
    const clap_audio_buffer_t *audio_inputs;
    clap_audio_buffer_t *audio_outputs;
    uint32_t audio_inputs_count;
    uint32_t audio_outputs_count;
    const clap_input_events_t *in_events;
    const clap_output_events_t *out_events;
} clap_process_t;

typedef struct clap_plugin {
    const clap_plugin_descriptor_t *desc;
    void *plugin_data;
    bool(CLAP_ABI *init)(const struct clap_plugin *plugin);
    void(CLAP_ABI *destroy)(const struct clap_plugin *plugin);
    bool(CLAP_ABI *activate)(const struct clap_plugin *plugin,
                             double sample_rate,
                             uint32_t min_frames_count,
                             uint32_t max_frames_count);
    void(CLAP_ABI *deactivate)(const struct clap_plugin *plugin);
    bool(CLAP_ABI *start_processing)(const struct clap_plugin *plugin);
    void(CLAP_ABI *stop_processing)(const struct clap_plugin *plugin);
    void(CLAP_ABI *reset)(const struct clap_plugin *plugin);
    clap_process_status(CLAP_ABI *process)(const struct clap_plugin *plugin, const clap_process_t *process);
    const void *(CLAP_ABI *get_extension)(const struct clap_plugin *plugin, const char *id);
    void(CLAP_ABI *on_main_thread)(const struct clap_plugin *plugin);
} clap_plugin_t;

typedef struct clap_plugin_factory {
    uint32_t(CLAP_ABI *get_plugin_count)(const struct clap_plugin_factory *factory);
    const clap_plugin_descriptor_t *(CLAP_ABI *get_plugin_descriptor)(const struct clap_plugin_factory *factory,
                                                                      uint32_t index);
    const clap_plugin_t *(CLAP_ABI *create_plugin)(const struct clap_plugin_factory *factory,
                                                   const clap_host_t *host,
                                                   const char *plugin_id);
} clap_plugin_factory_t;

// Extensions: audio-ports
static const char CLAP_EXT_AUDIO_PORTS[] = "clap.audio-ports";
static const char CLAP_PORT_STEREO[] = "stereo";

enum {
    CLAP_AUDIO_PORT_IS_MAIN = 1 << 0,
    CLAP_AUDIO_PORT_SUPPORTS_64BITS = 1 << 1,
    CLAP_AUDIO_PORT_PREFERS_64BITS = 1 << 2,
    CLAP_AUDIO_PORT_REQUIRES_COMMON_SAMPLE_RATE = 1 << 3
};

typedef struct clap_audio_port_info {
    clap_id id;
    char name[CLAP_NAME_SIZE];
    uint32_t flags;
    uint32_t channel_count;
    const char *port_type;
    clap_id in_place_pair;
} clap_audio_port_info_t;

typedef struct clap_plugin_audio_ports {
    uint32_t(CLAP_ABI *count)(const clap_plugin_t *plugin, bool is_input);
    bool(CLAP_ABI *get)(const clap_plugin_t *plugin,
                        uint32_t index,
                        bool is_input,
                        clap_audio_port_info_t *info);
} clap_plugin_audio_ports_t;

// Extensions: note-ports
static const char CLAP_EXT_NOTE_PORTS[] = "clap.note-ports";

enum clap_note_dialect {
    CLAP_NOTE_DIALECT_CLAP = 1 << 0,
    CLAP_NOTE_DIALECT_MIDI = 1 << 1,
    CLAP_NOTE_DIALECT_MIDI_MPE = 1 << 2,
    CLAP_NOTE_DIALECT_MIDI2 = 1 << 3
};

typedef struct clap_note_port_info {
    clap_id id;
    uint32_t supported_dialects;
    uint32_t preferred_dialect;
    char name[CLAP_NAME_SIZE];
} clap_note_port_info_t;

typedef struct clap_plugin_note_ports {
    uint32_t(CLAP_ABI *count)(const clap_plugin_t *plugin, bool is_input);
    bool(CLAP_ABI *get)(const clap_plugin_t *plugin,
                        uint32_t index,
                        bool is_input,
                        clap_note_port_info_t *info);
} clap_plugin_note_ports_t;

// Extensions: params
static const char CLAP_EXT_PARAMS[] = "clap.params";

enum {
    CLAP_PARAM_IS_STEPPED = 1 << 0,
    CLAP_PARAM_IS_PERIODIC = 1 << 1,
    CLAP_PARAM_IS_HIDDEN = 1 << 2,
    CLAP_PARAM_IS_READONLY = 1 << 3,
    CLAP_PARAM_IS_BYPASS = 1 << 4,
    CLAP_PARAM_IS_AUTOMATABLE = 1 << 5,
    CLAP_PARAM_IS_AUTOMATABLE_PER_NOTE_ID = 1 << 6,
    CLAP_PARAM_IS_AUTOMATABLE_PER_KEY = 1 << 7,
    CLAP_PARAM_IS_AUTOMATABLE_PER_CHANNEL = 1 << 8,
    CLAP_PARAM_IS_AUTOMATABLE_PER_PORT = 1 << 9,
    CLAP_PARAM_IS_MODULATABLE = 1 << 10,
    CLAP_PARAM_IS_MODULATABLE_PER_NOTE_ID = 1 << 11,
    CLAP_PARAM_IS_MODULATABLE_PER_KEY = 1 << 12,
    CLAP_PARAM_IS_MODULATABLE_PER_CHANNEL = 1 << 13,
    CLAP_PARAM_IS_MODULATABLE_PER_PORT = 1 << 14,
    CLAP_PARAM_REQUIRES_PROCESS = 1 << 15
};

typedef uint32_t clap_param_info_flags;

typedef struct clap_param_info {
    clap_id id;
    clap_param_info_flags flags;
    void *cookie;
    char name[CLAP_NAME_SIZE];
    char module[CLAP_PATH_SIZE];
    double min_value;
    double max_value;
    double default_value;
} clap_param_info_t;

typedef struct clap_plugin_params {
    uint32_t(CLAP_ABI *count)(const clap_plugin_t *plugin);
    bool(CLAP_ABI *get_info)(const clap_plugin_t *plugin, uint32_t param_index, clap_param_info_t *param_info);
    bool(CLAP_ABI *get_value)(const clap_plugin_t *plugin, clap_id param_id, double *out_value);
    bool(CLAP_ABI *value_to_text)(const clap_plugin_t *plugin,
                                  clap_id param_id,
                                  double value,
                                  char *out_buffer,
                                  uint32_t out_buffer_capacity);
    bool(CLAP_ABI *text_to_value)(const clap_plugin_t *plugin,
                                  clap_id param_id,
                                  const char *param_value_text,
                                  double *out_value);
    void(CLAP_ABI *flush)(const clap_plugin_t *plugin,
                          const clap_input_events_t *in,
                          const clap_output_events_t *out);
} clap_plugin_params_t;

// Extensions: state
static const char CLAP_EXT_STATE[] = "clap.state";

typedef struct clap_istream {
    void *ctx;
    int64_t(CLAP_ABI *read)(const struct clap_istream *stream, void *buffer, uint64_t size);
} clap_istream_t;

typedef struct clap_ostream {
    void *ctx;
    int64_t(CLAP_ABI *write)(const struct clap_ostream *stream, const void *buffer, uint64_t size);
} clap_ostream_t;

typedef struct clap_plugin_state {
    bool(CLAP_ABI *save)(const clap_plugin_t *plugin, const clap_ostream_t *stream);
    bool(CLAP_ABI *load)(const clap_plugin_t *plugin, const clap_istream_t *stream);
} clap_plugin_state_t;

// Extensions: gui
static const char CLAP_EXT_GUI[] = "clap.gui";
static const char CLAP_WINDOW_API_WIN32[] = "win32";
static const char CLAP_WINDOW_API_COCOA[] = "cocoa";
static const char CLAP_WINDOW_API_X11[] = "x11";
static const char CLAP_WINDOW_API_WAYLAND[] = "wayland";

typedef void *clap_hwnd;
typedef void *clap_nsview;
typedef unsigned long clap_xwnd;

typedef struct clap_window {
    const char *api;
    union {
        clap_nsview cocoa;
        clap_xwnd x11;
        clap_hwnd win32;
        void *ptr;
    };
} clap_window_t;

typedef struct clap_gui_resize_hints {
    bool can_resize_horizontally;
    bool can_resize_vertically;
    bool preserve_aspect_ratio;
    uint32_t aspect_ratio_width;
    uint32_t aspect_ratio_height;
} clap_gui_resize_hints_t;

typedef struct clap_plugin_gui {
    bool(CLAP_ABI *is_api_supported)(const clap_plugin_t *plugin, const char *api, bool is_floating);
    bool(CLAP_ABI *get_preferred_api)(const clap_plugin_t *plugin, const char **api, bool *is_floating);
    bool(CLAP_ABI *create)(const clap_plugin_t *plugin, const char *api, bool is_floating);
    void(CLAP_ABI *destroy)(const clap_plugin_t *plugin);
    bool(CLAP_ABI *set_scale)(const clap_plugin_t *plugin, double scale);
    bool(CLAP_ABI *get_size)(const clap_plugin_t *plugin, uint32_t *width, uint32_t *height);
    bool(CLAP_ABI *can_resize)(const clap_plugin_t *plugin);
    bool(CLAP_ABI *get_resize_hints)(const clap_plugin_t *plugin, clap_gui_resize_hints_t *hints);
    bool(CLAP_ABI *adjust_size)(const clap_plugin_t *plugin, uint32_t *width, uint32_t *height);
    bool(CLAP_ABI *set_size)(const clap_plugin_t *plugin, uint32_t width, uint32_t height);
    bool(CLAP_ABI *set_parent)(const clap_plugin_t *plugin, const clap_window_t *window);
    bool(CLAP_ABI *set_transient)(const clap_plugin_t *plugin, const clap_window_t *window);
    void(CLAP_ABI *suggest_title)(const clap_plugin_t *plugin, const char *title);
    bool(CLAP_ABI *show)(const clap_plugin_t *plugin);
    bool(CLAP_ABI *hide)(const clap_plugin_t *plugin);
} clap_plugin_gui_t;

#ifdef __cplusplus
}
#endif
