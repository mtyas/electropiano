#if defined(__APPLE__)
#import <Cocoa/Cocoa.h>
#include "../gui/PhsCocoaView.h"
#elif defined(_WIN32)
#include "../gui/PhsWin32View.h"
#elif defined(__linux__)
#include "../gui/PhsX11View.h"
#endif

#include "../../include/clap/clap.h"
#include "../dsp/PhsSynthEngine.hpp"
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <atomic>

namespace {

static const char *const kClapFeatures[] = {
    "instrument",
    "synthesizer",
    "electric-piano",
    "piano",
    "stereo",
    nullptr
};

static const clap_plugin_descriptor_t kClapDescriptor = {
    CLAP_VERSION,
    "com.mtyas.electropiano",
    "electropiano",
    "mtyas",
    "https://mtyas.com",
    "https://mtyas.com",
    "https://ko-fi.com/mtyas",
    "0.79",
    "electropiano by mtyas - Port-Hamiltonian Rhodes & Wurlitzer Physical Model",
    kClapFeatures
};

struct GuiParamEvent {
    uint32_t paramId;
    float normValue;
};

// clap.gui host extension (the parts used here), declared with the official ABI
struct clap_host_gui_ext {
    void(CLAP_ABI *resize_hints_changed)(const clap_host_t *host);
    bool(CLAP_ABI *request_resize)(const clap_host_t *host, uint32_t width, uint32_t height);
    bool(CLAP_ABI *request_show)(const clap_host_t *host);
    bool(CLAP_ABI *request_hide)(const clap_host_t *host);
    void(CLAP_ABI *closed)(const clap_host_t *host, bool was_destroyed);
};

struct PhsClapPlugin {
    clap_plugin_t plugin{};
    const clap_host_t *host = nullptr;
    phs::PhsSynthEngine engine{};
    // Editor size in pixels (follows the saved zoom, the zoom control and host resizes)
    uint32_t editorW = 0, editorH = 0;

    // The editor's zoom control asks the host to resize its window
    bool requestEditorResize(int w, int h) {
        const auto *gui = host && host->get_extension
                        ? static_cast<const clap_host_gui_ext *>(host->get_extension(host, "clap.gui")) : nullptr;
        if (!gui || !gui->request_resize || !gui->request_resize(host, static_cast<uint32_t>(w), static_cast<uint32_t>(h))) return false;
        editorW = static_cast<uint32_t>(w);
        editorH = static_cast<uint32_t>(h);
        return true;
    }
#if defined(__APPLE__)
    NSView *editorView = nil;
#elif defined(_WIN32)
    HWND editorView = nullptr;
#elif defined(__linux__)
    Window editorView = 0;
#else
    void *editorView = nullptr;
#endif

    // Lock-free SPSC ring buffer for GUI -> Host parameter automation notifications
    GuiParamEvent guiParamRing[64]{};
    std::atomic<uint32_t> guiParamWrite{0};
    std::atomic<uint32_t> guiParamRead{0};

    void pushGuiParamChange(uint32_t id, float normVal) {
        const uint32_t w = guiParamWrite.load(std::memory_order_relaxed);
        const uint32_t nextW = (w + 1) & 63;
        if (nextW != guiParamRead.load(std::memory_order_acquire)) {
            guiParamRing[w] = {id, normVal};
            guiParamWrite.store(nextW, std::memory_order_release);
        }
        if (host && host->request_process) {
            host->request_process(host);
        }
    }

    void flushGuiParamChanges(const clap_output_events_t *out) {
        if (!out || !out->try_push) return;
        uint32_t r = guiParamRead.load(std::memory_order_relaxed);
        const uint32_t w = guiParamWrite.load(std::memory_order_acquire);
        while (r != w) {
            const auto ev = guiParamRing[r];
            clap_event_param_value_t pev{};
            pev.header.size = sizeof(clap_event_param_value_t);
            pev.header.time = 0;
            pev.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
            pev.header.type = CLAP_EVENT_PARAM_VALUE;
            pev.header.flags = CLAP_EVENT_IS_LIVE;
            pev.param_id = ev.paramId;
            pev.cookie = nullptr;
            pev.note_id = -1;
            pev.port_index = -1;
            pev.channel = -1;
            pev.key = -1;
            pev.value = static_cast<double>(ev.normValue);
            out->try_push(out, &pev.header);
            r = (r + 1) & 63;
        }
        guiParamRead.store(r, std::memory_order_release);
    }

    void processInputEvent(const clap_event_header_t *hdr) {
        if (!hdr || hdr->space_id != CLAP_CORE_EVENT_SPACE_ID) return;
        switch (hdr->type) {
            case CLAP_EVENT_NOTE_ON: {
                const auto *ev = reinterpret_cast<const clap_event_note_t *>(hdr);
                engine.noteOn(ev->key, static_cast<float>(ev->velocity), ev->channel < 0 ? 0 : ev->channel);
                break;
            }
            case CLAP_EVENT_NOTE_OFF:
            case CLAP_EVENT_NOTE_CHOKE: {
                const auto *ev = reinterpret_cast<const clap_event_note_t *>(hdr);
                engine.noteOff(ev->key, ev->channel < 0 ? 0 : ev->channel,
                               hdr->type == CLAP_EVENT_NOTE_CHOKE ? 1.0f : static_cast<float>(ev->velocity));
                break;
            }
            case CLAP_EVENT_NOTE_EXPRESSION: {
                const auto *ev = reinterpret_cast<const clap_event_note_expression_t *>(hdr);
                if (ev->key < 0 || ev->key > 127) break;
                const float value = static_cast<float>(ev->value);
                if (ev->expression_id == CLAP_NOTE_EXPRESSION_TUNING) engine.setNoteTuning(ev->key, value);
                else if (ev->expression_id == CLAP_NOTE_EXPRESSION_PRESSURE) engine.setNotePressure(ev->key, value);
                else if (ev->expression_id == CLAP_NOTE_EXPRESSION_BRIGHTNESS) engine.setNoteBrightness(ev->key, value);
                break;
            }
            case CLAP_EVENT_MIDI_SYSEX: {
                // clap_event_midi_sysex: header, port_index, buffer, size
                struct SysexEvent { clap_event_header_t header; uint16_t port_index; const uint8_t *buffer; uint32_t size; };
                const auto *ev = reinterpret_cast<const SysexEvent *>(hdr);
                if (ev->buffer && ev->size > 0) engine.handleSysEx(ev->buffer, ev->size);
                break;
            }
            case CLAP_EVENT_MIDI: {
                const auto *ev = reinterpret_cast<const clap_event_midi_t *>(hdr);
                engine.handleMidiEvent(ev->data[0], ev->data[1], ev->data[2]);
                break;
            }
            case CLAP_EVENT_PARAM_MOD: {
                const auto *ev = reinterpret_cast<const clap_event_param_mod_t *>(hdr);
                // Only global modulation: per-note / per-key targets are not supported
                if (ev->note_id == -1 && ev->key == -1 && ev->channel == -1) {
                    engine.setHostModulation(ev->param_id, static_cast<float>(ev->amount));
                }
                break;
            }
            case CLAP_EVENT_PARAM_VALUE: {
                const auto *ev = reinterpret_cast<const clap_event_param_value_t *>(hdr);
                engine.setParamNormalized(ev->param_id, static_cast<float>(ev->value));
                break;
            }
            default:
                break;
        }
    }
};

// --- Audio Ports Extension ---
static uint32_t CLAP_ABI audioPortsCount(const clap_plugin_t *, bool is_input) {
    return is_input ? 0 : 1;
}

static bool CLAP_ABI audioPortsGet(const clap_plugin_t *,
                                   uint32_t index,
                                   bool is_input,
                                   clap_audio_port_info_t *info) {
    if (is_input || index != 0 || !info) return false;
    std::memset(info, 0, sizeof(*info));
    info->id = 0;
    std::snprintf(info->name, sizeof(info->name), "Main Output");
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = 2;
    info->port_type = CLAP_PORT_STEREO;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}

static const clap_plugin_audio_ports_t kClapAudioPortsExt = {
    audioPortsCount,
    audioPortsGet
};

// --- Note Ports Extension ---
static uint32_t CLAP_ABI notePortsCount(const clap_plugin_t *, bool is_input) {
    return is_input ? 1 : 0;
}

static bool CLAP_ABI notePortsGet(const clap_plugin_t *,
                                  uint32_t index,
                                  bool is_input,
                                  clap_note_port_info_t *info) {
    if (!is_input || index != 0 || !info) return false;
    std::memset(info, 0, sizeof(*info));
    info->id = 0;
    info->supported_dialects = CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI;
    info->preferred_dialect = CLAP_NOTE_DIALECT_CLAP;
    std::snprintf(info->name, sizeof(info->name), "Notes In");
    return true;
}

static const clap_plugin_note_ports_t kClapNotePortsExt = {
    notePortsCount,
    notePortsGet
};

// --- Params Extension ---
static uint32_t CLAP_ABI paramsCount(const clap_plugin_t *) {
    return phs::kNumParams;
}

static bool CLAP_ABI paramsGetInfo(const clap_plugin_t *,
                                   uint32_t param_index,
                                   clap_param_info_t *info) {
    if (param_index >= phs::kNumParams || !info) return false;
    const auto &meta = phs::kParamMetadata[param_index];
    std::memset(info, 0, sizeof(*info));
    info->id = meta.id;
    // Values are exchanged normalised (0..1), so choice parameters are not flagged
    // CLAP_PARAM_IS_STEPPED (which would restrict them to the integers 0 and 1).
    // Continuous parameters accept host modulation (CLAP_EVENT_PARAM_MOD): an offset added to the
    // knob without moving it, applied to the whole instrument
    info->flags = CLAP_PARAM_IS_AUTOMATABLE | (meta.steps == 0 ? CLAP_PARAM_IS_MODULATABLE : 0);
    info->cookie = nullptr;
    std::snprintf(info->name, sizeof(info->name), "%s", meta.name);
    std::snprintf(info->module, sizeof(info->module), "%s", meta.category);
    info->min_value = 0.0;
    info->max_value = 1.0;
    info->default_value = static_cast<double>(meta.defaultValue);
    return true;
}

static bool CLAP_ABI paramsGetValue(const clap_plugin_t *plugin, clap_id param_id, double *out_value) {
    if (!plugin || !out_value || param_id >= phs::kNumParams) return false;
    auto *self = static_cast<PhsClapPlugin *>(plugin->plugin_data);
    *out_value = static_cast<double>(self->engine.getParamNormalized(param_id));
    return true;
}

static bool CLAP_ABI paramsValueToText(const clap_plugin_t *,
                                       clap_id param_id,
                                       double value,
                                       char *out_buf,
                                       uint32_t out_buf_cap) {
    if (param_id >= phs::kNumParams || !out_buf || out_buf_cap == 0) return false;
    phs::formatParamDisplay(param_id, static_cast<float>(value), out_buf, out_buf_cap);
    return true;
}

static bool CLAP_ABI paramsTextToValue(const clap_plugin_t *,
                                       clap_id param_id,
                                       const char *text,
                                       double *out_value) {
    if (param_id >= phs::kNumParams || !text || !out_value) return false;
    *out_value = std::clamp(std::atof(text) / 100.0, 0.0, 1.0);
    return true;
}

static void CLAP_ABI paramsFlush(const clap_plugin_t *plugin,
                                 const clap_input_events_t *in,
                                 const clap_output_events_t *out) {
    auto *self = static_cast<PhsClapPlugin *>(plugin->plugin_data);
    if (in && in->size && in->get) {
        const uint32_t n = in->size(in);
        for (uint32_t i = 0; i < n; ++i) {
            self->processInputEvent(in->get(in, i));
        }
    }
    self->flushGuiParamChanges(out);
}

static const clap_plugin_params_t kClapParamsExt = {
    paramsCount,
    paramsGetInfo,
    paramsGetValue,
    paramsValueToText,
    paramsTextToValue,
    paramsFlush
};

// --- State Extension ---
static bool CLAP_ABI stateSave(const clap_plugin_t *plugin, const clap_ostream_t *stream) {
    if (!plugin || !stream || !stream->write) return false;
    auto *self = static_cast<PhsClapPlugin *>(plugin->plugin_data);
    const std::vector<uint8_t> st = self->engine.saveState();
    size_t done = 0;
    while (done < st.size()) {
        const int64_t written = stream->write(stream, st.data() + done, st.size() - done);
        if (written <= 0) return false;
        done += static_cast<size_t>(written);
    }
    return true;
}

static bool CLAP_ABI stateLoad(const clap_plugin_t *plugin, const clap_istream_t *stream) {
    if (!plugin || !stream || !stream->read) return false;
    auto *self = static_cast<PhsClapPlugin *>(plugin->plugin_data);
    // Older versions saved fewer parameters, so a shorter stream is fine: setState reads by version
    std::vector<uint8_t> st;
    uint8_t buf[4096];
    for (;;) {
        const int64_t n = stream->read(stream, buf, sizeof(buf));
        if (n <= 0 || st.size() > (8u << 20)) break;
        st.insert(st.end(), buf, buf + n);
    }
    if (st.size() < 12) return false;
    return self->engine.setState(st.data(), st.size());
}

static const clap_plugin_state_t kClapStateExt = {
    stateSave,
    stateLoad
};

// --- GUI Extension (Cocoa NSView / Win32 HWND / Linux X11) ---
static bool CLAP_ABI guiIsApiSupported(const clap_plugin_t *, const char *api, bool is_floating) {
    if (is_floating || !api) return false;
#if defined(__APPLE__)
    return std::strcmp(api, CLAP_WINDOW_API_COCOA) == 0;
#elif defined(_WIN32)
    return std::strcmp(api, CLAP_WINDOW_API_WIN32) == 0;
#elif defined(__linux__)
    return std::strcmp(api, CLAP_WINDOW_API_X11) == 0;
#else
    return false;
#endif
}

static bool CLAP_ABI guiGetPreferredApi(const clap_plugin_t *, const char **api, bool *is_floating) {
    if (!api || !is_floating) return false;
    *is_floating = false;
#if defined(__APPLE__)
    *api = CLAP_WINDOW_API_COCOA;
#elif defined(_WIN32)
    *api = CLAP_WINDOW_API_WIN32;
#elif defined(__linux__)
    *api = CLAP_WINDOW_API_X11;
#else
    return false;
#endif
    return true;
}

static bool CLAP_ABI guiCreate(const clap_plugin_t *plugin, const char *api, bool is_floating) {
    if (!guiIsApiSupported(plugin, api, is_floating)) return false;
    auto *self = static_cast<PhsClapPlugin *>(plugin->plugin_data);
#if defined(__APPLE__)
    if (!self->editorView) {
        self->editorView = phs::createPhsEditorView(&self->engine, [self](uint32_t id, float val) {
            self->pushGuiParamChange(id, val);
        }, [self](int w, int h) { return self->requestEditorResize(w, h); });
    }
    return self->editorView != nil;
#else
    return true;
#endif
}

static void CLAP_ABI guiDestroy(const clap_plugin_t *plugin) {
    auto *self = static_cast<PhsClapPlugin *>(plugin->plugin_data);
#if defined(__APPLE__)
    if (self->editorView) {
        phs::destroyPhsEditorView(self->editorView);
        self->editorView = nil;
    }
#elif defined(_WIN32)
    if (self->editorView) {
        phs::destroyPhsWin32EditorView(self->editorView);
        self->editorView = nullptr;
    }
#elif defined(__linux__)
    if (self->editorView) {
        phs::destroyPhsX11EditorView(self->editorView);
        self->editorView = 0;
    }
#endif
}

static bool CLAP_ABI guiSetScale(const clap_plugin_t *, double) { return true; }

static bool CLAP_ABI guiGetSize(const clap_plugin_t *plugin, uint32_t *width, uint32_t *height) {
    if (!width || !height) return false;
    auto *self = static_cast<PhsClapPlugin *>(plugin->plugin_data);
    if (self->editorW == 0) {
        const float z = phs::loadEditorZoom();
        self->editorW = static_cast<uint32_t>(phs::scaledEditorWidth(z));
        self->editorH = static_cast<uint32_t>(phs::scaledEditorHeight(z));
    }
    *width = self->editorW;
    *height = self->editorH;
    return true;
}

// Resizable at the layout's aspect ratio, from half to three times its size
static bool CLAP_ABI guiCanResize(const clap_plugin_t *) { return true; }

static bool CLAP_ABI guiGetResizeHints(const clap_plugin_t *, clap_gui_resize_hints_t *hints) {
    if (!hints) return false;
    hints->can_resize_horizontally = true;
    hints->can_resize_vertically = true;
    hints->preserve_aspect_ratio = true;
    hints->aspect_ratio_width = phs::kEditorWidth;
    hints->aspect_ratio_height = phs::kEditorHeight;
    return true;
}

static bool CLAP_ABI guiAdjustSize(const clap_plugin_t *, uint32_t *width, uint32_t *height) {
    if (!width || !height) return false;
    const float z = std::clamp(std::min(*width / static_cast<float>(phs::kEditorWidth), *height / static_cast<float>(phs::kEditorHeight)), 0.5f, 3.0f);
    *width = static_cast<uint32_t>(phs::scaledEditorWidth(z));
    *height = static_cast<uint32_t>(phs::scaledEditorHeight(z));
    return true;
}

static bool CLAP_ABI guiSetSize(const clap_plugin_t *plugin, uint32_t width, uint32_t height) {
    auto *self = static_cast<PhsClapPlugin *>(plugin->plugin_data);
    guiAdjustSize(plugin, &width, &height);
    self->editorW = width;
    self->editorH = height;
#if defined(__APPLE__)
    if (self->editorView) [self->editorView setFrameSize:NSMakeSize(width, height)];
#elif defined(_WIN32)
    if (self->editorView) phs::setPhsWin32EditorSize(self->editorView, static_cast<int>(width), static_cast<int>(height));
#elif defined(__linux__)
    if (self->editorView) phs::setPhsX11EditorSize(self->editorView, static_cast<int>(width), static_cast<int>(height));
#endif
    return true;
}

static bool CLAP_ABI guiSetParent(const clap_plugin_t *plugin, const clap_window_t *window) {
    if (!window) return false;
    auto *self = static_cast<PhsClapPlugin *>(plugin->plugin_data);
#if defined(__APPLE__)
    if (!window->cocoa) return false;
    if (!self->editorView) {
        guiCreate(plugin, CLAP_WINDOW_API_COCOA, false);
    }
    NSView *parentView = static_cast<NSView *>(window->cocoa);
    if (parentView && self->editorView) {
        [parentView addSubview:self->editorView];
        return true;
    }
    return false;
#elif defined(_WIN32)
    if (!window->win32) return false;
    HWND parentHwnd = static_cast<HWND>(window->win32);
    self->editorView = phs::createPhsWin32EditorView(parentHwnd, &self->engine, [self](uint32_t id, float val) {
        self->pushGuiParamChange(id, val);
    }, [self](int w, int h) { return self->requestEditorResize(w, h); });
    return self->editorView != nullptr;
#elif defined(__linux__)
    if (!window->x11) return false;
    Window parentX11 = static_cast<Window>(window->x11);
    // request_resize is thread-safe in CLAP, so the editor's own thread may ask
    self->editorView = phs::createPhsX11EditorView(parentX11, &self->engine, [self](uint32_t id, float val) {
        self->pushGuiParamChange(id, val);
    }, [self](int w, int h) { return self->requestEditorResize(w, h); });
    return self->editorView != 0;
#else
    return false;
#endif
}

static bool CLAP_ABI guiSetTransient(const clap_plugin_t *, const clap_window_t *) { return false; }
static void CLAP_ABI guiSuggestTitle(const clap_plugin_t *, const char *) {}

static bool CLAP_ABI guiShow(const clap_plugin_t *plugin) {
    auto *self = static_cast<PhsClapPlugin *>(plugin->plugin_data);
#if defined(__APPLE__)
    if (self->editorView) [self->editorView setHidden:NO];
#elif defined(_WIN32)
    if (self->editorView) ShowWindow(self->editorView, SW_SHOW);
#endif
    return true;
}

static bool CLAP_ABI guiHide(const clap_plugin_t *plugin) {
    auto *self = static_cast<PhsClapPlugin *>(plugin->plugin_data);
#if defined(__APPLE__)
    if (self->editorView) [self->editorView setHidden:YES];
#elif defined(_WIN32)
    if (self->editorView) ShowWindow(self->editorView, SW_HIDE);
#endif
    return true;
}

static const clap_plugin_gui_t kClapGuiExt = {
    guiIsApiSupported,
    guiGetPreferredApi,
    guiCreate,
    guiDestroy,
    guiSetScale,
    guiGetSize,
    guiCanResize,
    guiGetResizeHints,
    guiAdjustSize,
    guiSetSize,
    guiSetParent,
    guiSetTransient,
    guiSuggestTitle,
    guiShow,
    guiHide
};

// --- Core Plugin Lifecycle & Sample-Accurate Process ---
static bool CLAP_ABI pluginInit(const clap_plugin_t *) { return true; }

static void CLAP_ABI pluginDestroy(const clap_plugin_t *plugin) {
    if (!plugin) return;
    auto *self = static_cast<PhsClapPlugin *>(plugin->plugin_data);
    guiDestroy(plugin);
    delete self;
}

static bool CLAP_ABI pluginActivate(const clap_plugin_t *plugin, double sample_rate, uint32_t, uint32_t) {
    auto *self = static_cast<PhsClapPlugin *>(plugin->plugin_data);
    self->engine.prepare(static_cast<float>(sample_rate));
    return true;
}

static void CLAP_ABI pluginDeactivate(const clap_plugin_t *) {}
static bool CLAP_ABI pluginStartProcessing(const clap_plugin_t *) { return true; }
static void CLAP_ABI pluginStopProcessing(const clap_plugin_t *) {}

static void CLAP_ABI pluginReset(const clap_plugin_t *plugin) {
    auto *self = static_cast<PhsClapPlugin *>(plugin->plugin_data);
    self->engine.reset();
}

static clap_process_status CLAP_ABI pluginProcess(const clap_plugin_t *plugin, const clap_process_t *process) {
    auto *self = static_cast<PhsClapPlugin *>(plugin->plugin_data);
    const uint32_t numFrames = process->frames_count;
    const clap_input_events_t *inEvents = process->in_events;
    const uint32_t numEvents = (inEvents && inEvents->size) ? inEvents->size(inEvents) : 0;

    if (process->transport) {
        const auto *tr = static_cast<const clap_event_transport_t *>(process->transport);
        if (tr->flags & CLAP_TRANSPORT_HAS_TEMPO) self->engine.setTempo(static_cast<float>(tr->tempo));
    }

    float *outL = nullptr;
    float *outR = nullptr;
    if (process->audio_outputs_count > 0 && process->audio_outputs[0].data32) {
        outL = process->audio_outputs[0].data32[0];
        outR = (process->audio_outputs[0].channel_count > 1)
             ? process->audio_outputs[0].data32[1]
             : outL;
    }

    uint32_t curFrame = 0;
    uint32_t evIdx = 0;

    while (curFrame < numFrames) {
        while (evIdx < numEvents) {
            const clap_event_header_t *hdr = inEvents->get(inEvents, evIdx);
            if (hdr && hdr->time > curFrame) break;
            self->processInputEvent(hdr);
            ++evIdx;
        }
        uint32_t nextFrame = numFrames;
        if (evIdx < numEvents) {
            const clap_event_header_t *hdr = inEvents->get(inEvents, evIdx);
            if (hdr && hdr->time < nextFrame) {
                nextFrame = hdr->time;
            }
        }
        const uint32_t chunk = nextFrame - curFrame;
        if (chunk > 0) {
            self->engine.processBlock(outL ? (outL + curFrame) : nullptr,
                                      outR ? (outR + curFrame) : nullptr,
                                      static_cast<int>(chunk));
            curFrame = nextFrame;
        }
    }

    // Process any remaining events at end of block
    while (evIdx < numEvents) {
        self->processInputEvent(inEvents->get(inEvents, evIdx));
        ++evIdx;
    }

    self->flushGuiParamChanges(process->out_events);
    return CLAP_PROCESS_CONTINUE;
}

// clap.render (render.h) and clap.tail (tail.h), declared here with the official ABI
struct clap_plugin_render_ext {
    bool(CLAP_ABI *has_hard_realtime_requirement)(const clap_plugin_t *plugin);
    bool(CLAP_ABI *set)(const clap_plugin_t *plugin, int32_t mode); // 0 realtime, 1 offline
};
struct clap_plugin_tail_ext {
    uint32_t(CLAP_ABI *get)(const clap_plugin_t *plugin);
};
static bool CLAP_ABI renderHasHardRealtime(const clap_plugin_t *) { return false; }
static bool CLAP_ABI renderSet(const clap_plugin_t *plugin, int32_t mode) {
    static_cast<PhsClapPlugin *>(plugin->plugin_data)->engine.setOfflineRendering(mode == 1);
    return true;
}
static uint32_t CLAP_ABI tailGet(const clap_plugin_t *plugin) {
    // Long enough for the slowest reverb / echo tail; the engine goes idle on its own after it
    const auto *self = static_cast<const PhsClapPlugin *>(plugin->plugin_data);
    return static_cast<uint32_t>(self->engine.sampleRate() * 8.0f);
}
static const clap_plugin_render_ext kClapRenderExt = {renderHasHardRealtime, renderSet};
static const clap_plugin_tail_ext kClapTailExt = {tailGet};

static const void *CLAP_ABI pluginGetExtension(const clap_plugin_t *, const char *id) {
    if (!id) return nullptr;
    if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) return &kClapAudioPortsExt;
    if (std::strcmp(id, CLAP_EXT_NOTE_PORTS) == 0)  return &kClapNotePortsExt;
    if (std::strcmp(id, CLAP_EXT_PARAMS) == 0)      return &kClapParamsExt;
    if (std::strcmp(id, CLAP_EXT_STATE) == 0)       return &kClapStateExt;
    if (std::strcmp(id, CLAP_EXT_GUI) == 0)         return &kClapGuiExt;
    if (std::strcmp(id, "clap.render") == 0)        return &kClapRenderExt;
    if (std::strcmp(id, "clap.tail") == 0)          return &kClapTailExt;
    return nullptr;
}

static void CLAP_ABI pluginOnMainThread(const clap_plugin_t *) {}

// --- Factory ---
static uint32_t CLAP_ABI factoryGetPluginCount(const clap_plugin_factory_t *) {
    return 1;
}

static const clap_plugin_descriptor_t *CLAP_ABI factoryGetPluginDescriptor(const clap_plugin_factory_t *,
                                                                           uint32_t index) {
    return (index == 0) ? &kClapDescriptor : nullptr;
}

static const clap_plugin_t *CLAP_ABI factoryCreatePlugin(const clap_plugin_factory_t *,
                                                         const clap_host_t *host,
                                                         const char *plugin_id) {
    if (!host || !plugin_id || std::strcmp(plugin_id, kClapDescriptor.id) != 0) {
        return nullptr;
    }
    auto *instance = new PhsClapPlugin();
    instance->host = host;
    instance->plugin.desc = &kClapDescriptor;
    instance->plugin.plugin_data = instance;
    instance->plugin.init = pluginInit;
    instance->plugin.destroy = pluginDestroy;
    instance->plugin.activate = pluginActivate;
    instance->plugin.deactivate = pluginDeactivate;
    instance->plugin.start_processing = pluginStartProcessing;
    instance->plugin.stop_processing = pluginStopProcessing;
    instance->plugin.reset = pluginReset;
    instance->plugin.process = pluginProcess;
    instance->plugin.get_extension = pluginGetExtension;
    instance->plugin.on_main_thread = pluginOnMainThread;
    return &instance->plugin;
}

static const clap_plugin_factory_t kClapFactory = {
    factoryGetPluginCount,
    factoryGetPluginDescriptor,
    factoryCreatePlugin
};

static bool CLAP_ABI entryInit(const char *) { return true; }
static void CLAP_ABI entryDeinit(void) {}
static const void *CLAP_ABI entryGetFactory(const char *factory_id) {
    if (factory_id && std::strcmp(factory_id, CLAP_PLUGIN_FACTORY_ID) == 0) {
        return &kClapFactory;
    }
    return nullptr;
}

} // namespace

extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry = {
    CLAP_VERSION,
    entryInit,
    entryDeinit,
    entryGetFactory
};
