// The parts of the LV2 C ABI electropiano uses: core plugin descriptor, URID map, atom
// sequences / objects, state, and the UI extension. Declared here (as the CLAP and VST3 ABIs
// are in include/) so building needs no SDK; layouts follow lv2/core/lv2.h, urid/urid.h,
// atom/atom.h, state/state.h and ui/ui.h of the LV2 specification (ISC licence).
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32)
#define LV2_SYMBOL_EXPORT __declspec(dllexport)
#else
#define LV2_SYMBOL_EXPORT __attribute__((visibility("default")))
#endif

// ---- core -----------------------------------------------------------------------------------
typedef void* LV2_Handle;
typedef struct { const char* URI; void* data; } LV2_Feature;

typedef struct LV2_Descriptor {
    const char* URI;
    LV2_Handle (*instantiate)(const struct LV2_Descriptor* descriptor, double sample_rate, const char* bundle_path,
                              const LV2_Feature* const* features);
    void (*connect_port)(LV2_Handle instance, uint32_t port, void* data_location);
    void (*activate)(LV2_Handle instance);
    void (*run)(LV2_Handle instance, uint32_t sample_count);
    void (*deactivate)(LV2_Handle instance);
    void (*cleanup)(LV2_Handle instance);
    const void* (*extension_data)(const char* uri);
} LV2_Descriptor;

// ---- urid -----------------------------------------------------------------------------------
#define LV2_URID__map "http://lv2plug.in/ns/ext/urid#map"
typedef uint32_t LV2_URID;
typedef void* LV2_URID_Map_Handle;
typedef struct {
    LV2_URID_Map_Handle handle;
    LV2_URID (*map)(LV2_URID_Map_Handle handle, const char* uri);
} LV2_URID_Map;

// ---- atom -----------------------------------------------------------------------------------
#define LV2_ATOM__Chunk "http://lv2plug.in/ns/ext/atom#Chunk"
#define LV2_ATOM__Object "http://lv2plug.in/ns/ext/atom#Object"
#define LV2_ATOM__Blank "http://lv2plug.in/ns/ext/atom#Blank"
#define LV2_ATOM__Float "http://lv2plug.in/ns/ext/atom#Float"
#define LV2_ATOM__Double "http://lv2plug.in/ns/ext/atom#Double"
#define LV2_MIDI__MidiEvent "http://lv2plug.in/ns/ext/midi#MidiEvent"
#define LV2_TIME__Position "http://lv2plug.in/ns/ext/time#Position"
#define LV2_TIME__beatsPerMinute "http://lv2plug.in/ns/ext/time#beatsPerMinute"

typedef struct { uint32_t size; uint32_t type; } LV2_Atom;
typedef struct { LV2_Atom atom; float body; } LV2_Atom_Float;
typedef struct { LV2_Atom atom; double body; } LV2_Atom_Double;
typedef struct {
    union { int64_t frames; double beats; } time;
    LV2_Atom body;
} LV2_Atom_Event;
typedef struct { uint32_t unit; uint32_t pad; } LV2_Atom_Sequence_Body;
typedef struct { LV2_Atom atom; LV2_Atom_Sequence_Body body; } LV2_Atom_Sequence;
typedef struct { uint32_t id; uint32_t otype; } LV2_Atom_Object_Body;
typedef struct { LV2_Atom atom; LV2_Atom_Object_Body body; } LV2_Atom_Object;
typedef struct { uint32_t key; uint32_t context; LV2_Atom value; } LV2_Atom_Property_Body;

static inline uint32_t lv2_atom_pad_size(uint32_t size) { return (size + 7U) & (~7U); }

// ---- state ----------------------------------------------------------------------------------
#define LV2_STATE__interface "http://lv2plug.in/ns/ext/state#interface"
typedef void* LV2_State_Handle;
typedef enum { LV2_STATE_SUCCESS = 0, LV2_STATE_ERR_UNKNOWN = 1, LV2_STATE_ERR_BAD_TYPE = 2,
               LV2_STATE_ERR_BAD_FLAGS = 3, LV2_STATE_ERR_NO_FEATURE = 4, LV2_STATE_ERR_NO_PROPERTY = 5 } LV2_State_Status;
typedef enum { LV2_STATE_IS_POD = 1, LV2_STATE_IS_PORTABLE = 2, LV2_STATE_IS_NATIVE = 4 } LV2_State_Flags;
typedef LV2_State_Status (*LV2_State_Store_Function)(LV2_State_Handle handle, uint32_t key, const void* value,
                                                      size_t size, uint32_t type, uint32_t flags);
typedef const void* (*LV2_State_Retrieve_Function)(LV2_State_Handle handle, uint32_t key, size_t* size,
                                                    uint32_t* type, uint32_t* flags);
typedef struct {
    LV2_State_Status (*save)(LV2_Handle instance, LV2_State_Store_Function store, LV2_State_Handle handle,
                             uint32_t flags, const LV2_Feature* const* features);
    LV2_State_Status (*restore)(LV2_Handle instance, LV2_State_Retrieve_Function retrieve, LV2_State_Handle handle,
                                uint32_t flags, const LV2_Feature* const* features);
} LV2_State_Interface;

// ---- ui -------------------------------------------------------------------------------------
#define LV2_UI__parent "http://lv2plug.in/ns/extensions/ui#parent"
#define LV2_UI__resize "http://lv2plug.in/ns/extensions/ui#resize"
#define LV2_INSTANCE_ACCESS_URI "http://lv2plug.in/ns/ext/instance-access"
typedef void* LV2UI_Widget;
typedef void* LV2UI_Handle;
typedef void* LV2UI_Controller;
typedef void* LV2UI_Feature_Handle;
typedef void (*LV2UI_Write_Function)(LV2UI_Controller controller, uint32_t port_index, uint32_t buffer_size,
                                     uint32_t port_protocol, const void* buffer);
typedef struct LV2UI_Descriptor {
    const char* URI;
    LV2UI_Handle (*instantiate)(const struct LV2UI_Descriptor* descriptor, const char* plugin_uri, const char* bundle_path,
                                LV2UI_Write_Function write_function, LV2UI_Controller controller, LV2UI_Widget* widget,
                                const LV2_Feature* const* features);
    void (*cleanup)(LV2UI_Handle ui);
    void (*port_event)(LV2UI_Handle ui, uint32_t port_index, uint32_t buffer_size, uint32_t format, const void* buffer);
    const void* (*extension_data)(const char* uri);
} LV2UI_Descriptor;
typedef struct {
    LV2UI_Feature_Handle handle;
    int (*ui_resize)(LV2UI_Feature_Handle handle, int width, int height);
} LV2UI_Resize;

#ifdef __cplusplus
}
#endif
