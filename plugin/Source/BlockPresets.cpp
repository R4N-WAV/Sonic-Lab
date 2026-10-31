#include "BlockPresets.h"

#include <map>

namespace blockpresets
{
namespace
{
    using P = std::vector<std::pair<std::string, float>>;

    Preset make (const char* name, P params, float mix = -1.0f, float outDb = 0.0f)
    {
        Preset p;
        p.name = name;
        p.params = std::move (params);
        p.mix = mix;
        p.outDb = outDb;
        return p;
    }

    // EQ band list -> parameter pairs. shape: 0 bell, 1 lo shelf, 2 hi shelf, 3 lo cut, 4 hi cut, 5 notch, 6 band pass, 7 tilt.
    struct B { int shape; float freq, gain, q; int slope = 3; bool dyn = false; float thr = -30, range = -6; };
    P bands (std::initializer_list<B> list)
    {
        P out;
        int i = 1;
        for (auto& b : list)
        {
            const std::string k = "b" + std::to_string (i++) + "_";
            out.push_back ({ k + "on", 1.0f });
            out.push_back ({ k + "shape", (float) b.shape });
            out.push_back ({ k + "freq_hz", b.freq });
            out.push_back ({ k + "gain_db", b.gain });
            out.push_back ({ k + "q", b.q });
            out.push_back ({ k + "slope", (float) b.slope });
            if (b.dyn)
            {
                out.push_back ({ k + "dyn", 1.0f });
                out.push_back ({ k + "dyn_thr_db", b.thr });
                out.push_back ({ k + "dyn_range_db", b.range });
            }
        }
        return out;
    }

    const std::map<std::string, std::vector<Preset>>& table()
    {
        static const std::map<std::string, std::vector<Preset>> t = {
            { "tube.stage", {
                make ("Clean triode warmth", { { "drive_db", 4 }, { "bias", 0.15f }, { "character", 0.1f }, { "sag", 0.2f } }),
                make ("Pushed preamp", { { "drive_db", 14 }, { "bias", 0.2f }, { "character", 0.25f }, { "sag", 0.45f } }),
                make ("Pentode grit", { { "drive_db", 18 }, { "character", 0.85f }, { "sag", 0.3f } }),
                make ("Dark and round", { { "drive_db", 10 }, { "character", 0.2f }, { "sag", 0.4f }, { "hf_rolloff_hz", 7000 } }),
                make ("Breathing bus", { { "drive_db", 6 }, { "character", 0.15f }, { "sag", 0.7f } }) } },
            { "character.tape", {
                make ("15 ips studio", { { "speed", 1 }, { "formulation", 1 }, { "input_db", 3 }, { "saturation", 0.5f }, { "head_bump", 0.5f }, { "hf_loss", 0.4f }, { "wow", 0.03f }, { "flutter", 0.04f } }),
                make ("30 ips master", { { "speed", 2 }, { "formulation", 2 }, { "input_db", 2 }, { "saturation", 0.35f }, { "head_bump", 0.4f }, { "hf_loss", 0.15f }, { "wow", 0.02f }, { "flutter", 0.02f } }),
                make ("Hot glue", { { "speed", 1 }, { "formulation", 2 }, { "input_db", 9 }, { "saturation", 0.65f } }),
                make ("7.5 ips lo-fi", { { "speed", 0 }, { "formulation", 0 }, { "input_db", 8 }, { "saturation", 0.7f }, { "head_bump", 0.6f }, { "hf_loss", 0.75f }, { "wow", 0.35f }, { "flutter", 0.3f }, { "noise_db", -70 } }),
                make ("Worn cassette", { { "speed", 0 }, { "formulation", 0 }, { "input_db", 6 }, { "bias", 0.35f }, { "saturation", 0.6f }, { "head_bump", 0.3f }, { "hf_loss", 0.85f }, { "wow", 0.55f }, { "flutter", 0.5f }, { "noise_db", -52 } }) } },
            { "character.transformer", {
                make ("Clean nickel", { { "profile", 0 }, { "drive_db", 2 }, { "core_sat", 0.3f } }),
                make ("Steel console input", { { "profile", 1 }, { "drive_db", 8 }, { "core_sat", 0.5f } }),
                make ("Iron vintage", { { "profile", 2 }, { "drive_db", 12 }, { "core_sat", 0.75f }, { "lf_corner_hz", 25 }, { "hf_lp_hz", 18000 } }),
                make ("Big low end", { { "profile", 2 }, { "drive_db", 16 }, { "core_sat", 0.9f } }) } },
            { "character.console", {
                make ("British channel", { { "model", 1 }, { "drive_db", 6 } }),
                make ("American punch", { { "model", 2 }, { "drive_db", 8 } }),
                make ("Clean summing", { { "model", 0 }, { "drive_db", 2 }, { "crosstalk_db", -80 } }),
                make ("Driven British", { { "model", 1 }, { "drive_db", 14 }, { "noise_db", -90 }, { "tolerance", 0.5f } }) } },
            { "character.saturation", {
                make ("Silky triode", { { "character", 0 }, { "drive_db", 10 } }),
                make ("Warm glue", { { "character", 1 }, { "drive_db", 8 } }),
                make ("Thick transformer", { { "character", 2 }, { "drive_db", 14 } }),
                make ("Fuzz", { { "character", 3 }, { "drive_db", 24 } }),
                make ("Air shimmer", { { "character", 6 }, { "drive_db", 12 }, { "focus_lo_hz", 2000 } }),
                make ("Mid grit, clean lows", { { "character", 5 }, { "drive_db", 16 }, { "focus_lo_hz", 200 }, { "focus_hi_hz", 5000 } }) } },
            { "dynamics.compressor", {
                make ("Opto leveller (LA-2A style)", { { "model", 0 }, { "threshold_db", -24 }, { "ratio", 3 }, { "attack_ms", 10 }, { "release_ms", 500 }, { "knee_db", 10 }, { "program_dep", 0.7f }, { "color", 0.3f }, { "makeup_db", 4 } }),
                make ("FET grab (1176 style)", { { "model", 1 }, { "threshold_db", -20 }, { "ratio", 4 }, { "attack_ms", 0.8f }, { "release_ms", 120 }, { "knee_db", 2 }, { "color", 0.5f }, { "makeup_db", 5 } }),
                make ("VCA bus glue (SSL style)", { { "model", 2 }, { "threshold_db", -16 }, { "ratio", 4 }, { "attack_ms", 10 }, { "release_ms", 100 }, { "auto_release", 1 }, { "knee_db", 6 }, { "sc_hpf_hz", 80 }, { "makeup_db", 2 } }),
                make ("Vari-Mu smooth (Fairchild style)", { { "model", 3 }, { "threshold_db", -20 }, { "ratio", 6 }, { "attack_ms", 20 }, { "release_ms", 800 }, { "knee_db", 12 }, { "color", 0.45f }, { "makeup_db", 3 } }),
                make ("Diode colour (33609 style)", { { "model", 4 }, { "threshold_db", -18 }, { "ratio", 4 }, { "attack_ms", 5 }, { "release_ms", 250 }, { "color", 0.6f }, { "makeup_db", 3 } }),
                make ("Parallel smash", { { "model", 1 }, { "threshold_db", -40 }, { "ratio", 20 }, { "attack_ms", 0.5f }, { "release_ms", 60 }, { "makeup_db", 10 } }, 0.35f),
                make ("Sidechain duck", { { "model", 2 }, { "threshold_db", -30 }, { "ratio", 8 }, { "attack_ms", 1 }, { "release_ms", 180 }, { "knee_db", 3 }, { "key", 1 } }) } },
            { "dynamics.transient", {
                make ("Snappy drums", { { "attack_pct", 40 }, { "sustain_pct", -10 } }),
                make ("Tighten tails", { { "sustain_pct", -50 } }),
                make ("Soften hits", { { "attack_pct", -40 }, { "sustain_pct", 10 } }),
                make ("Room bloom", { { "attack_pct", 10 }, { "sustain_pct", 45 }, { "release_ms", 400 } }) } },
            { "tone.eq", {
                make ("Air and body", bands ({ { 1, 100, 2, 0.7f }, { 0, 350, -1.5f, 1.2f }, { 2, 10000, 3, 0.7f } })),
                make ("Vocal presence", bands ({ { 3, 90, 0, 0.71f, 3 }, { 0, 300, -2, 1.4f }, { 0, 3000, 2.5f, 1.0f }, { 2, 12000, 2, 0.7f } })),
                make ("Kick tidy", bands ({ { 3, 30, 0, 0.71f, 3 }, { 0, 60, 3, 1.2f }, { 0, 350, -4, 1.6f }, { 0, 3500, 3, 1.4f } })),
                make ("Mud cut", bands ({ { 0, 250, -3, 1.0f }, { 0, 500, -1.5f, 1.4f } })),
                make ("Telephone", bands ({ { 3, 400, 0, 0.71f, 3 }, { 4, 3200, 0, 0.71f, 3 }, { 0, 1500, 4, 1.0f } })),
                make ("De-harsh (dynamic)", bands ({ { 0, 3500, 0, 2.0f, 3, true, -30, -6 } })) } },
            { "tone.filter", {
                make ("Warm low-pass", { { "type", 0 }, { "slope", 1 }, { "cutoff_hz", 6000 }, { "resonance", 0.2f } }),
                make ("Clean high-pass", { { "type", 1 }, { "slope", 1 }, { "cutoff_hz", 120 }, { "resonance", 0 } }),
                make ("Resonant sweep", { { "type", 0 }, { "slope", 0 }, { "cutoff_hz", 800 }, { "resonance", 0.7f }, { "lfo_rate_hz", 0.25f }, { "lfo_depth_oct", 2 } }),
                make ("Auto-wah", { { "type", 2 }, { "cutoff_hz", 600 }, { "resonance", 0.6f }, { "env_oct", 2.5f } }),
                make ("Telephone band", { { "type", 2 }, { "cutoff_hz", 1500 }, { "resonance", 0.3f } }) } },
            { "tone.exciter", {
                make ("Silky air", { { "freq_hz", 8000 }, { "drive_db", 6 }, { "character", 0 }, { "amount_db", -14 } }),
                make ("Crisp presence", { { "freq_hz", 3000 }, { "drive_db", 10 }, { "character", 1 }, { "amount_db", -16 } }),
                make ("Vocal sheen", { { "freq_hz", 5000 }, { "drive_db", 8 }, { "character", 2 }, { "amount_db", -12 } }),
                make ("Bright drums", { { "freq_hz", 2500 }, { "drive_db", 12 }, { "character", 1 }, { "amount_db", -10 } }) } },
            { "space.delay", {
                make ("Slapback", { { "time_ms", 110 }, { "feedback_pct", 8 }, { "lp_hz", 5000 }, { "hp_hz", 150 } }, 0.25f),
                make ("Dotted eighth", { { "sync", 1 }, { "division", 6 }, { "feedback_pct", 35 }, { "lp_hz", 6000 } }, 0.25f),
                make ("Ping-pong quarter", { { "sync", 1 }, { "division", 8 }, { "feedback_pct", 40 }, { "ping_pong", 1 } }, 0.25f),
                make ("Tape echo", { { "time_ms", 375 }, { "feedback_pct", 45 }, { "lp_hz", 3500 }, { "wow", 0.4f }, { "drive", 0.5f } }, 0.3f),
                make ("Ducked vocal throw", { { "sync", 1 }, { "division", 8 }, { "feedback_pct", 30 }, { "ducking", 0.6f } }, 0.3f),
                make ("Doubler", { { "time_ms", 18 }, { "feedback_pct", 0 }, { "offset_ms", 9 } }, 0.35f) } },
            { "space.reverb", {
                make ("Small room", { { "predelay_ms", 5 }, { "size", 0.25f }, { "decay_s", 0.6f }, { "damping_hz", 7000 }, { "early", 0.5f } }, 0.18f),
                make ("Vocal plate", { { "predelay_ms", 25 }, { "size", 0.5f }, { "decay_s", 1.8f }, { "damping_hz", 9000 }, { "diffusion", 0.85f }, { "early", 0.1f } }, 0.2f),
                make ("Large hall", { { "predelay_ms", 30 }, { "size", 0.85f }, { "decay_s", 3.2f }, { "damping_hz", 5500 }, { "low_mult", 1.2f }, { "early", 0.35f } }, 0.22f),
                make ("Dark chamber", { { "predelay_ms", 15 }, { "size", 0.6f }, { "decay_s", 2.0f }, { "damping_hz", 3500 } }, 0.2f),
                make ("Ambient wash", { { "predelay_ms", 60 }, { "size", 1.0f }, { "decay_s", 9.0f }, { "damping_hz", 6000 }, { "mod_depth", 0.6f } }, 0.35f) } },
            { "mod.chorus", {
                make ("Subtle widen", { { "voices", 1 }, { "rate_hz", 0.4f }, { "depth_ms", 1.5f }, { "delay_ms", 10 } }, 0.3f),
                make ("Classic 80s", { { "voices", 1 }, { "rate_hz", 0.8f }, { "depth_ms", 3.5f }, { "delay_ms", 8 } }, 0.5f),
                make ("Ensemble", { { "voices", 3 }, { "rate_hz", 0.6f }, { "depth_ms", 4 }, { "delay_ms", 14 } }, 0.5f),
                make ("Vibrato", { { "voices", 0 }, { "rate_hz", 5 }, { "depth_ms", 2 }, { "delay_ms", 4 } }, 1.0f) } },
            { "mod.flanger", {
                make ("Jet", { { "rate_hz", 0.12f }, { "depth", 0.9f }, { "delay_ms", 2.5f }, { "feedback_pct", 70 } }),
                make ("Subtle swirl", { { "rate_hz", 0.25f }, { "depth", 0.5f }, { "delay_ms", 3 }, { "feedback_pct", 30 } }),
                make ("Hollow", { { "rate_hz", 0.3f }, { "depth", 0.7f }, { "delay_ms", 2 }, { "feedback_pct", -70 } }),
                make ("Slow and deep", { { "rate_hz", 0.08f }, { "depth", 1.0f }, { "delay_ms", 0.3f }, { "feedback_pct", 50 } }) } },
            { "mod.phaser", {
                make ("Classic 4-stage", { { "stages", 1 }, { "rate_hz", 0.5f }, { "depth", 0.8f }, { "centre_hz", 800 }, { "feedback_pct", 30 } }),
                make ("Slow 12-stage", { { "stages", 5 }, { "rate_hz", 0.15f }, { "feedback_pct", 50 } }),
                make ("Fast vibe", { { "stages", 2 }, { "rate_hz", 4 }, { "depth", 0.6f }, { "feedback_pct", 20 } }) } },
            { "pitch.shift", {
                make ("Octave up", { { "semitones", 12 } }),
                make ("Octave down", { { "semitones", -12 } }),
                make ("Fifth up", { { "semitones", 7 } }),
                make ("Wide doubler", { { "spread_cents", 12 } }, 0.5f),
                make ("Shimmer feed", { { "semitones", 12 }, { "feedback", 0.5f }, { "tone_hz", 8000 } }, 0.3f) } },
            { "utility.stereo", {
                make ("Mono", { { "width_pct", 0 } }),
                make ("Wide, mono lows", { { "width_pct", 130 }, { "bass_mono_hz", 120 } }),
                make ("Tight low end", { { "bass_mono_hz", 150 } }),
                make ("Centre focus", { { "mid_db", 1 }, { "side_db", -4 } }) } },
            { "utility.gain", {
                make ("-6 dB", { { "gain_db", -6 } }),
                make ("Polarity flip", { { "invert_l", 1 }, { "invert_r", 1 } }),
                make ("Mono", { { "mono", 1 } }) } },
        };
        return t;
    }
}

const std::vector<Preset>& factory (const std::string& type)
{
    static const std::vector<Preset> none;
    auto it = table().find (type);
    return it == table().end() ? none : it->second;
}

juce::File folder (const std::string& type)
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("Atelier").getChildFile ("Block presets").getChildFile (juce::String (type));
}

std::vector<Preset> personal (const std::string& type)
{
    std::vector<Preset> out;
    for (auto& f : folder (type).findChildFiles (juce::File::findFiles, false, "*.atelier-block"))
    {
        auto xml = juce::XmlDocument::parse (f);
        if (xml == nullptr || ! xml->hasTagName ("BLOCKPRESET"))
            continue;
        Preset p;
        p.factory = false;
        p.file = f;
        p.name = xml->getStringAttribute ("name", f.getFileNameWithoutExtension());
        p.mix = (float) xml->getDoubleAttribute ("mix", -1.0);
        p.outDb = (float) xml->getDoubleAttribute ("out", 0.0);
        for (auto* e : xml->getChildWithTagNameIterator ("P"))
            p.params.push_back ({ e->getStringAttribute ("id").toStdString(), (float) e->getDoubleAttribute ("v") });
        out.push_back (std::move (p));
    }
    std::sort (out.begin(), out.end(), [] (const Preset& a, const Preset& b) { return a.name.compareNatural (b.name) < 0; });
    return out;
}

void apply (const Preset& p, sonic::Graph::Node& node)
{
    auto& m = *node.module;
    m.resetParamsToDefault();
    for (auto& [id, v] : p.params)
        if (m.paramIndex (id) >= 0)
            m.setParam (id, v);
    node.mix = p.mix >= 0.0f ? p.mix : m.defaultMix();
    node.outGainDb = p.outDb;
}

bool save (const sonic::Graph::Node& node, const juce::String& rawName, juce::String& error)
{
    const auto name = rawName.trim();
    if (name.isEmpty()) { error = "Please give the preset a name."; return false; }
    auto dir = folder (node.type);
    if (! dir.createDirectory()) { error = "Could not create " + dir.getFullPathName(); return false; }
    juce::XmlElement xml ("BLOCKPRESET");
    xml.setAttribute ("name", name);
    xml.setAttribute ("type", juce::String (node.type));
    xml.setAttribute ("mix", node.mix.load());
    xml.setAttribute ("out", node.outGainDb.load());
    const auto& m = *node.module;
    for (int i = 0; i < m.numParams(); ++i)
    {
        auto* e = xml.createNewChildElement ("P");
        e->setAttribute ("id", juce::String (m.params()[(size_t) i].id));
        e->setAttribute ("v", m.getParam (i));
    }
    const auto file = dir.getChildFile (juce::File::createLegalFileName (name) + ".atelier-block");
    if (! xml.writeTo (file)) { error = "Could not write " + file.getFullPathName(); return false; }
    return true;
}

bool remove (const Preset& p)
{
    if (p.factory || ! p.file.existsAsFile()) return false;
    return p.file.moveToTrash() || p.file.deleteFile();
}

} // namespace blockpresets
