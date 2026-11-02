// LV2 plugin and UI for electropiano.
//
// Ports: 0 MIDI in (atom sequence: MIDI, SysEx tuning, host tempo), 1-2 audio out,
// 3.. one control port per parameter, normalised 0..1 like every other format. Notes and
// controller changes are applied at their sample position; parameters at block start. The
// complete state (parameters, MIDI map, microtuning) is saved through the state extension.
// The UI reaches the engine through instance-access and embeds the shared editor
// (X11 on Linux, Cocoa on macOS, Win32 on Windows). Metadata: tools/gen_lv2_ttl.cpp.

#include "../../include/lv2/lv2_abi.h"
#include "../dsp/PhsSynthEngine.hpp"
#if defined(__APPLE__)
#include "../gui/PhsCocoaView.h"
#elif defined(_WIN32)
#include "../gui/PhsWin32View.h"
#elif defined(__linux__)
#include "../gui/PhsX11View.h"
#endif
#include <cstring>
#include <memory>

namespace {

constexpr const char* kPluginUri = "https://mtyas.com/plugins/electropiano";
constexpr const char* kUiUri = "https://mtyas.com/plugins/electropiano#ui";
constexpr const char* kStateKeyUri = "https://mtyas.com/plugins/electropiano#state";
constexpr uint32_t kPortMidi = 0, kPortOutL = 1, kPortOutR = 2, kFirstParamPort = 3;

struct Lv2Plugin {
    phs::PhsSynthEngine engine;
    const LV2_Atom_Sequence* midiIn = nullptr;
    float* outL = nullptr;
    float* outR = nullptr;
    const float* paramPorts[phs::kNumParams]{};
    float lastPort[phs::kNumParams]{};
    LV2_URID uMidi = 0, uObject = 0, uBlank = 0, uPosition = 0, uBpm = 0, uFloat = 0, uDouble = 0, uChunk = 0, uStateKey = 0;
};

LV2_Handle instantiate(const LV2_Descriptor*, double rate, const char*, const LV2_Feature* const* features) {
    const LV2_URID_Map* map = nullptr;
    for (int i = 0; features && features[i]; ++i) {
        if (std::strcmp(features[i]->URI, LV2_URID__map) == 0) map = static_cast<const LV2_URID_Map*>(features[i]->data);
    }
    if (!map) return nullptr; // urid:map is a required feature
    auto* p = new Lv2Plugin();
    p->engine.prepare(static_cast<float>(rate));
    auto m = [map](const char* uri) { return map->map(map->handle, uri); };
    p->uMidi = m(LV2_MIDI__MidiEvent);
    p->uObject = m(LV2_ATOM__Object);
    p->uBlank = m(LV2_ATOM__Blank);
    p->uPosition = m(LV2_TIME__Position);
    p->uBpm = m(LV2_TIME__beatsPerMinute);
    p->uFloat = m(LV2_ATOM__Float);
    p->uDouble = m(LV2_ATOM__Double);
    p->uChunk = m(LV2_ATOM__Chunk);
    p->uStateKey = m(kStateKeyUri);
    for (uint32_t i = 0; i < phs::kNumParams; ++i) p->lastPort[i] = -1.0f;
    return p;
}

void connectPort(LV2_Handle h, uint32_t port, void* data) {
    auto* p = static_cast<Lv2Plugin*>(h);
    if (port == kPortMidi) p->midiIn = static_cast<const LV2_Atom_Sequence*>(data);
    else if (port == kPortOutL) p->outL = static_cast<float*>(data);
    else if (port == kPortOutR) p->outR = static_cast<float*>(data);
    else if (port >= kFirstParamPort && port < kFirstParamPort + phs::kNumParams) p->paramPorts[port - kFirstParamPort] = static_cast<const float*>(data);
}

void activate(LV2_Handle h) { static_cast<Lv2Plugin*>(h)->engine.reset(); }

// Host tempo from a time:Position object
void readPosition(Lv2Plugin* p, const LV2_Atom_Object* obj) {
    const auto* prop = reinterpret_cast<const LV2_Atom_Property_Body*>(obj + 1);
    const char* end = reinterpret_cast<const char*>(&obj->body) + obj->atom.size;
    while (reinterpret_cast<const char*>(prop) < end) {
        if (prop->key == p->uBpm) {
            if (prop->value.type == p->uFloat) p->engine.setTempo(reinterpret_cast<const LV2_Atom_Float*>(&prop->value)->body);
            else if (prop->value.type == p->uDouble) p->engine.setTempo(static_cast<float>(reinterpret_cast<const LV2_Atom_Double*>(&prop->value)->body));
        }
        prop = reinterpret_cast<const LV2_Atom_Property_Body*>(reinterpret_cast<const char*>(prop) + sizeof(LV2_Atom_Property_Body) +
                                                               lv2_atom_pad_size(prop->value.size));
    }
}

void run(LV2_Handle h, uint32_t n) {
    auto* p = static_cast<Lv2Plugin*>(h);
    // Control ports that moved since the last block (the editor writes them too, so a knob
    // turned in the UI comes back here unchanged and is not re-applied)
    for (uint32_t i = 0; i < phs::kNumParams; ++i) {
        if (!p->paramPorts[i]) continue;
        const float v = *p->paramPorts[i];
        if (v != p->lastPort[i]) {
            p->lastPort[i] = v;
            p->engine.setParamNormalized(i, v);
        }
    }
    if (!p->outL || !p->outR) return;

    uint32_t cur = 0;
    if (p->midiIn) {
        const auto* ev = reinterpret_cast<const LV2_Atom_Event*>(p->midiIn + 1);
        const char* end = reinterpret_cast<const char*>(&p->midiIn->body) + p->midiIn->atom.size;
        while (reinterpret_cast<const char*>(ev) < end) {
            const uint32_t at = static_cast<uint32_t>(std::min<int64_t>(std::max<int64_t>(ev->time.frames, 0), n));
            if (at > cur) {
                p->engine.processBlock(p->outL + cur, p->outR + cur, static_cast<int>(at - cur));
                cur = at;
            }
            const auto* data = reinterpret_cast<const uint8_t*>(&ev->body + 1);
            if (ev->body.type == p->uMidi && ev->body.size > 0) {
                if (data[0] == 0xF0) p->engine.handleSysEx(data, ev->body.size);
                else p->engine.handleMidiEvent(data[0], ev->body.size > 1 ? data[1] : 0, ev->body.size > 2 ? data[2] : 0);
            } else if (ev->body.type == p->uObject || ev->body.type == p->uBlank) {
                const auto* obj = reinterpret_cast<const LV2_Atom_Object*>(&ev->body);
                if (obj->body.otype == p->uPosition) readPosition(p, obj);
            }
            ev = reinterpret_cast<const LV2_Atom_Event*>(reinterpret_cast<const char*>(ev) + sizeof(LV2_Atom_Event) +
                                                         lv2_atom_pad_size(ev->body.size));
        }
    }
    if (cur < n) p->engine.processBlock(p->outL + cur, p->outR + cur, static_cast<int>(n - cur));
}

void cleanup(LV2_Handle h) { delete static_cast<Lv2Plugin*>(h); }

LV2_State_Status stateSave(LV2_Handle h, LV2_State_Store_Function store, LV2_State_Handle handle, uint32_t,
                           const LV2_Feature* const*) {
    auto* p = static_cast<Lv2Plugin*>(h);
    const std::vector<uint8_t> blob = p->engine.saveState();
    return store(handle, p->uStateKey, blob.data(), blob.size(), p->uChunk, LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE);
}

LV2_State_Status stateRestore(LV2_Handle h, LV2_State_Retrieve_Function retrieve, LV2_State_Handle handle, uint32_t,
                              const LV2_Feature* const*) {
    auto* p = static_cast<Lv2Plugin*>(h);
    size_t size = 0;
    uint32_t type = 0, flags = 0;
    const void* data = retrieve(handle, p->uStateKey, &size, &type, &flags);
    if (!data || type != p->uChunk) return LV2_STATE_ERR_NO_PROPERTY;
    return p->engine.setState(data, size) ? LV2_STATE_SUCCESS : LV2_STATE_ERR_UNKNOWN;
}

const LV2_State_Interface kState = {stateSave, stateRestore};

const void* extensionData(const char* uri) {
    return std::strcmp(uri, LV2_STATE__interface) == 0 ? &kState : nullptr;
}

const LV2_Descriptor kDescriptor = {kPluginUri, instantiate, connectPort, activate, run, nullptr, cleanup, extensionData};

// ---- UI ---------------------------------------------------------------------------------------
struct Lv2Ui {
#if defined(__APPLE__)
    NSView* view = nullptr;
#elif defined(_WIN32)
    HWND view = nullptr;
#elif defined(__linux__)
    Window view = 0;
#endif
};

LV2UI_Handle uiInstantiate(const LV2UI_Descriptor*, const char*, const char*, LV2UI_Write_Function write,
                           LV2UI_Controller controller, LV2UI_Widget* widget, const LV2_Feature* const* features) {
    void* parent = nullptr;
    Lv2Plugin* plugin = nullptr;
    const LV2UI_Resize* resize = nullptr;
    for (int i = 0; features && features[i]; ++i) {
        if (std::strcmp(features[i]->URI, LV2_UI__parent) == 0) parent = features[i]->data;
        else if (std::strcmp(features[i]->URI, LV2_INSTANCE_ACCESS_URI) == 0) plugin = static_cast<Lv2Plugin*>(features[i]->data);
        else if (std::strcmp(features[i]->URI, LV2_UI__resize) == 0) resize = static_cast<const LV2UI_Resize*>(features[i]->data);
    }
    if (!plugin) return nullptr; // instance-access is required
    // Knob moves go to the engine (by the editor) and to the host's control port, so the host
    // records and automates them
    auto onParam = [write, controller](uint32_t id, float v) { write(controller, kFirstParamPort + id, sizeof(float), 0, &v); };
    auto onResize = [resize](int w, int h) { return resize && resize->ui_resize(resize->handle, w, h) == 0; };
    auto* ui = new Lv2Ui();
#if defined(__APPLE__)
    ui->view = phs::createPhsEditorView(&plugin->engine, onParam, onResize);
    if (parent) phs::addPhsEditorViewToParent(ui->view, parent);
    *widget = ui->view;
#elif defined(_WIN32)
    ui->view = phs::createPhsWin32EditorView(static_cast<HWND>(parent), &plugin->engine, onParam, onResize);
    *widget = ui->view;
#elif defined(__linux__)
    ui->view = phs::createPhsX11EditorView(static_cast<Window>(reinterpret_cast<uintptr_t>(parent)), &plugin->engine, onParam, onResize);
    *widget = reinterpret_cast<LV2UI_Widget>(static_cast<uintptr_t>(ui->view));
#endif
    if (resize) {
        const float z = phs::loadEditorZoom();
        resize->ui_resize(resize->handle, phs::scaledEditorWidth(z), phs::scaledEditorHeight(z));
    }
    return ui;
}

void uiCleanup(LV2UI_Handle h) {
    auto* ui = static_cast<Lv2Ui*>(h);
#if defined(__APPLE__)
    if (ui->view) phs::destroyPhsEditorView(ui->view);
    phs::releasePhsEditorView(ui->view);
#elif defined(_WIN32)
    if (ui->view) phs::destroyPhsWin32EditorView(ui->view);
#elif defined(__linux__)
    if (ui->view) phs::destroyPhsX11EditorView(ui->view);
#endif
    delete ui;
}

// The editor reads the engine directly, so control port echoes need no handling
void uiPortEvent(LV2UI_Handle, uint32_t, uint32_t, uint32_t, const void*) {}
const void* uiExtensionData(const char*) { return nullptr; }

const LV2UI_Descriptor kUiDescriptor = {kUiUri, uiInstantiate, uiCleanup, uiPortEvent, uiExtensionData};

} // namespace

extern "C" LV2_SYMBOL_EXPORT const LV2_Descriptor* lv2_descriptor(uint32_t index) { return index == 0 ? &kDescriptor : nullptr; }
extern "C" LV2_SYMBOL_EXPORT const LV2UI_Descriptor* lv2ui_descriptor(uint32_t index) { return index == 0 ? &kUiDescriptor : nullptr; }
