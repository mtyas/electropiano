// Writes the LV2 bundle's metadata (manifest.ttl and electropiano.ttl) from the parameter
// table, so the LV2 ports always match the engine.
//
//   gen_lv2_ttl <bundle dir> <binary file name> <ui type: X11UI | CocoaUI | WindowsUI>
#include "../src/dsp/PhsParameters.hpp"
#include <cctype>
#include <cstdio>
#include <fstream>
#include <string>

int main(int argc, char** argv) {
    if (argc < 4) { std::fprintf(stderr, "usage: gen_lv2_ttl <bundle dir> <binary> <ui type>\n"); return 1; }
    const std::string dir = argv[1], binary = argv[2], uiType = argv[3];
    const char* uri = "https://mtyas.com/plugins/electropiano";

    std::ofstream man(dir + "/manifest.ttl");
    man << "@prefix lv2: <http://lv2plug.in/ns/lv2core#> .\n"
           "@prefix rdfs: <http://www.w3.org/2000/01/rdf-schema#> .\n"
           "@prefix ui: <http://lv2plug.in/ns/extensions/ui#> .\n\n"
        << "<" << uri << ">\n    a lv2:Plugin, lv2:InstrumentPlugin ;\n    lv2:binary <" << binary << "> ;\n"
        << "    rdfs:seeAlso <electropiano.ttl> .\n\n"
        << "<" << uri << "#ui>\n    a ui:" << uiType << " ;\n    ui:binary <" << binary << "> ;\n"
        << "    rdfs:seeAlso <electropiano.ttl> .\n";

    std::ofstream t(dir + "/electropiano.ttl");
    t << "@prefix atom: <http://lv2plug.in/ns/ext/atom#> .\n"
         "@prefix doap: <http://usefulinc.com/ns/doap#> .\n"
         "@prefix foaf: <http://xmlns.com/foaf/0.1/> .\n"
         "@prefix lv2: <http://lv2plug.in/ns/lv2core#> .\n"
         "@prefix midi: <http://lv2plug.in/ns/ext/midi#> .\n"
         "@prefix pprops: <http://lv2plug.in/ns/ext/port-props#> .\n"
         "@prefix rdfs: <http://www.w3.org/2000/01/rdf-schema#> .\n"
         "@prefix state: <http://lv2plug.in/ns/ext/state#> .\n"
         "@prefix time: <http://lv2plug.in/ns/ext/time#> .\n"
         "@prefix ui: <http://lv2plug.in/ns/extensions/ui#> .\n"
         "@prefix urid: <http://lv2plug.in/ns/ext/urid#> .\n\n"
      << "<" << uri << ">\n"
      << "    a lv2:Plugin, lv2:InstrumentPlugin ;\n"
      << "    doap:name \"electropiano\" ;\n"
      << "    doap:license <https://www.gnu.org/licenses/gpl-3.0> ;\n"
      << "    doap:maintainer [ foaf:name \"mtyas\" ; foaf:homepage <https://mtyas.com> ] ;\n"
      << "    lv2:minorVersion " << PHS_VERSION_MINOR << " ; lv2:microVersion 0 ;\n"
      << "    lv2:requiredFeature urid:map ;\n"
      << "    lv2:optionalFeature lv2:hardRTCapable ;\n"
      << "    lv2:extensionData state:interface ;\n"
      << "    ui:ui <" << uri << "#ui> ;\n"
      << "    lv2:port [\n"
      << "        a lv2:InputPort, atom:AtomPort ;\n"
      << "        atom:bufferType atom:Sequence ;\n"
      << "        atom:supports midi:MidiEvent, time:Position ;\n"
      << "        lv2:designation lv2:control ;\n"
      << "        lv2:index 0 ; lv2:symbol \"midi_in\" ; lv2:name \"MIDI In\"\n"
      << "    ] , [\n"
      << "        a lv2:OutputPort, lv2:AudioPort ; lv2:index 1 ; lv2:symbol \"out_l\" ; lv2:name \"Left\"\n"
      << "    ] , [\n"
      << "        a lv2:OutputPort, lv2:AudioPort ; lv2:index 2 ; lv2:symbol \"out_r\" ; lv2:name \"Right\"\n"
      << "    ]";
    for (uint32_t i = 0; i < phs::kNumParams; ++i) {
        const auto& m = phs::kParamMetadata[i];
        std::string sym = "p_";
        for (const char* c = m.shortName; *c; ++c) sym += std::isalnum(static_cast<unsigned char>(*c)) ? *c : '_';
        sym += "_" + std::to_string(i); // ids stay unique and stable
        t << " , [\n        a lv2:InputPort, lv2:ControlPort ;\n"
          << "        lv2:index " << (3 + i) << " ; lv2:symbol \"" << sym << "\" ; lv2:name \"" << m.name << "\" ;\n"
          << "        lv2:default " << m.defaultValue << " ; lv2:minimum 0.0 ; lv2:maximum 1.0";
        if (m.steps > 0) t << " ;\n        pprops:rangeSteps " << (m.steps + 1);
        t << "\n    ]";
    }
    t << " .\n\n<" << uri << "#ui>\n    a ui:" << uiType << " ;\n"
      << "    lv2:requiredFeature <http://lv2plug.in/ns/ext/instance-access> ;\n"
      << "    lv2:optionalFeature ui:resize, ui:parent .\n";
    std::printf("wrote %s/manifest.ttl and electropiano.ttl (%u parameters)\n", dir.c_str(), phs::kNumParams);
    return 0;
}
