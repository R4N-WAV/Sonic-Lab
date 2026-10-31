#include "Templates.h"
#include "PluginProcessor.h"

#include <sonic/Graph.h>
#include <sonic/modules/ParametricEqModule.h>

using sonic::Graph;

namespace
{
    // ---------------------------------------------------------------- tiny builder

    using Params = std::vector<std::pair<std::string, float>>;

    struct Blk
    {
        std::string type;
        std::string name;
        Params params;
        float mix = -1.0f;      // -1 = block default
        float outDb = 0.0f;
        std::function<void (sonic::Module&)> custom = {};
    };

    constexpr float kRowY = 132.0f;      // matches the Bridge's main line (world y 180 - card/2)
    constexpr float kStepX = 236.0f;

    int place (Graph& g, const Blk& b, float x, float y)
    {
        const int id = g.addNode (b.type, x, y);
        if (auto* n = g.node (id))
        {
            n->name = b.name;
            for (auto& [k, v] : b.params)
                n->module->setParam (k, v);
            if (b.custom) b.custom (*n->module);
            if (b.mix >= 0.0f) n->mix = b.mix;
            n->outGainDb = b.outDb;
        }
        return id;
    }

    // Input -> b0 -> b1 -> ... -> Output. Returns the ids.
    std::vector<int> serial (Graph& g, const std::vector<Blk>& blocks, float x0 = 210.0f, float y = kRowY, int from = Graph::kInputId, int to = Graph::kOutputId)
    {
        std::vector<int> ids;
        int prev = from;
        float x = x0;
        for (auto& b : blocks)
        {
            const int id = place (g, b, x, y);
            g.connect (prev, id);
            prev = id;
            ids.push_back (id);
            x += kStepX;
        }
        g.connect (prev, to);
        return ids;
    }

    // EQ band helper: shape 0 bell, 1 low shelf, 2 high shelf, 3 low cut, 4 high cut, 5 notch, 6 band pass, 7 tilt.
    // slope index: 0 = 6, 1 = 12, 2 = 18, 3 = 24 dB/oct ... ; place: 0 stereo, 1 L, 2 R, 3 mid, 4 side.
    struct Band { int shape; float freq, gain, q; int slope = 1; int place = 0; bool dyn = false; float thr = -30, range = -6, att = 10, rel = 150; };

    std::function<void (sonic::Module&)> bands (std::vector<Band> list)
    {
        return [list] (sonic::Module& m)
        {
            using EQ = sonic::ParametricEqModule;
            for (size_t i = 0; i < list.size() && i < (size_t) EQ::kMaxBands; ++i)
            {
                const auto& b = list[i];
                const int k = (int) i;
                m.setParam (EQ::bandParam (k, EQ::fOn), 1.0f);
                m.setParam (EQ::bandParam (k, EQ::fShape), (float) b.shape);
                m.setParam (EQ::bandParam (k, EQ::fFreq), b.freq);
                m.setParam (EQ::bandParam (k, EQ::fGain), b.gain);
                m.setParam (EQ::bandParam (k, EQ::fQ), b.q);
                m.setParam (EQ::bandParam (k, EQ::fSlope), (float) b.slope);
                m.setParam (EQ::bandParam (k, EQ::fPlace), (float) b.place);
                m.setParam (EQ::bandParam (k, EQ::fDyn), b.dyn ? 1.0f : 0.0f);
                m.setParam (EQ::bandParam (k, EQ::fThr), b.thr);
                m.setParam (EQ::bandParam (k, EQ::fRange), b.range);
                m.setParam (EQ::bandParam (k, EQ::fAtt), b.att);
                m.setParam (EQ::bandParam (k, EQ::fRel), b.rel);
            }
        };
    }

    // compressor models
    enum { opto = 0, fet = 1, vca = 2, varimu = 3, diode = 4 };

    templates::Template t (const char* cat, const char* name, const char* desc, std::function<void (Graph&)> build)
    {
        templates::Template x;
        x.category = cat;
        x.name = name;
        x.description = desc;
        x.factory = true;
        x.build = std::move (build);
        return x;
    }

    std::vector<templates::Template> makeFactory()
    {
        std::vector<templates::Template> v;
        const char* dyn = "Hardware - dynamics";
        const char* eq = "Hardware - EQ and colour";
        const char* tape = "Tape and character";
        const char* mix = "Mixing chains";
        const char* fx = "Creative effects";

        // ================================================ dynamics
        v.push_back (t (dyn, "LA-2A style leveler",
            "Optical leveling amp: ~10 ms attack, two-stage release (fast ~60 ms, then ~1.5 s), soft knee, about 3:1, "
            "through a tube make-up stage and output iron. Smooth, program-dependent vocal and bass leveling.",
            [] (Graph& g) { serial (g, {
                { "dynamics.compressor", "Opto leveler", { { "model", opto }, { "threshold_db", -22 }, { "ratio", 3 }, { "attack_ms", 10 },
                    { "release_ms", 1500 }, { "knee_db", 10 }, { "program_dep", 0.8f }, { "color", 0.35f }, { "auto_makeup", 1 }, { "detector", 1 } } },
                { "tube.stage", "12AX7 make-up", { { "drive_db", 4 }, { "bias", 0.15f }, { "character", 0.1f }, { "sag", 0.2f }, { "gain_comp", 1 } } },
                { "character.transformer", "Output iron", { { "profile", 1 }, { "drive_db", 2 }, { "core_sat", 0.35f } } } }); }));

        v.push_back (t (dyn, "LA-2A style (limit mode)",
            "The same optical cell switched to its limiting curve (higher ratio, firmer knee) - for catching peaks while keeping the opto smoothness.",
            [] (Graph& g) { serial (g, {
                { "dynamics.compressor", "Opto limit", { { "model", opto }, { "threshold_db", -18 }, { "ratio", 10 }, { "attack_ms", 10 },
                    { "release_ms", 1200 }, { "knee_db", 6 }, { "program_dep", 0.8f }, { "color", 0.35f }, { "auto_makeup", 1 } } },
                { "tube.stage", "12AX7 make-up", { { "drive_db", 4 }, { "bias", 0.15f }, { "character", 0.1f }, { "gain_comp", 1 } } } }); }));

        v.push_back (t (dyn, "1176 style (4:1, fast)",
            "FET limiting amplifier at 4:1: very fast attack (~0.2 ms), medium release, hard-ish knee, a little FET grit and output "
            "transformer. Vocals, bass and snare that need to sit forward.",
            [] (Graph& g) { serial (g, {
                { "dynamics.compressor", "FET 4:1", { { "model", fet }, { "threshold_db", -24 }, { "ratio", 4 }, { "attack_ms", 0.2f },
                    { "release_ms", 120 }, { "knee_db", 2 }, { "color", 0.45f }, { "auto_makeup", 1 } } },
                { "character.transformer", "Output iron", { { "profile", 1 }, { "drive_db", 3 }, { "core_sat", 0.4f } } } }); }));

        v.push_back (t (dyn, "1176 style all-buttons",
            "The 'British mode' trick: all ratio buttons in - an aggressive, distorted, pumping limiter with a recovering overshoot. "
            "Used in parallel (mix 50 %) on drums and room mics.",
            [] (Graph& g) { serial (g, {
                { "dynamics.compressor", "All buttons", { { "model", fet }, { "threshold_db", -34 }, { "ratio", 20 }, { "attack_ms", 0.1f },
                    { "release_ms", 80 }, { "knee_db", 0 }, { "color", 0.9f }, { "auto_makeup", 1 } }, 0.5f },
                { "character.saturation", "FET bite", { { "character", 5 }, { "drive_db", 8 }, { "auto_gain", 1 } }, 0.5f } }); }));

        v.push_back (t (dyn, "SSL G-bus style glue",
            "VCA bus compressor: 4:1, 10 ms attack, auto release, hard knee, stereo linked, followed by console colour. "
            "The classic mix-bus 'glue' - 2-4 dB of gain reduction is plenty.",
            [] (Graph& g) { serial (g, {
                { "dynamics.compressor", "Bus VCA", { { "model", vca }, { "threshold_db", -16 }, { "ratio", 4 }, { "attack_ms", 10 },
                    { "release_ms", 300 }, { "auto_release", 1 }, { "knee_db", 0 }, { "link_pct", 100 }, { "color", 0.2f }, { "auto_makeup", 1 } } },
                { "character.console", "Console bus", { { "model", 1 }, { "drive_db", 2 }, { "crosstalk_db", -70 } } } }); }));

        v.push_back (t (dyn, "Fairchild 670 style",
            "Variable-mu tube limiter, time constant 1: fast attack (~0.4 ms), ~300 ms release, ratio that rises with level, "
            "lots of tubes and iron. Thick, glued and slightly soft.",
            [] (Graph& g) { serial (g, {
                { "dynamics.compressor", "Vari-mu", { { "model", varimu }, { "threshold_db", -20 }, { "ratio", 6 }, { "attack_ms", 0.4f },
                    { "release_ms", 300 }, { "knee_db", 12 }, { "color", 0.55f }, { "auto_makeup", 1 } } },
                { "tube.stage", "6386 tubes", { { "drive_db", 6 }, { "bias", 0.1f }, { "character", 0.3f }, { "sag", 0.35f }, { "gain_comp", 1 } } },
                { "character.transformer", "Fairchild iron", { { "profile", 2 }, { "drive_db", 4 }, { "core_sat", 0.55f }, { "lf_corner_hz", 20 } } } }); }));

        v.push_back (t (dyn, "API 2500 style punch",
            "Fast VCA bus compressor with 'thrust' (the detector hears less low end), so kick and bass push through: 4:1, 10 ms attack, 100 ms release, hard knee.",
            [] (Graph& g) { serial (g, {
                { "dynamics.compressor", "Thrust VCA", { { "model", vca }, { "threshold_db", -18 }, { "ratio", 4 }, { "attack_ms", 10 },
                    { "release_ms", 100 }, { "knee_db", 0 }, { "sc_hpf_hz", 120 }, { "color", 0.3f }, { "auto_makeup", 1 } } },
                { "character.console", "API colour", { { "model", 2 }, { "drive_db", 3 } } } }); }));

        v.push_back (t (dyn, "Distressor style (Dist 2)",
            "Modern digitally controlled compressor emulating tube character: 6:1, 5 ms attack, 120 ms release, with added 2nd-harmonic "
            "'Dist 2' colour. Drums and aggressive vocals.",
            [] (Graph& g) { serial (g, {
                { "dynamics.compressor", "Distress", { { "model", vca }, { "threshold_db", -24 }, { "ratio", 6 }, { "attack_ms", 5 },
                    { "release_ms", 120 }, { "knee_db", 3 }, { "auto_makeup", 1 } } },
                { "character.saturation", "Dist 2 (even)", { { "character", 0 }, { "drive_db", 12 }, { "auto_gain", 1 } }, 0.6f } }); }));

        // ================================================ EQ and colour
        v.push_back (t (eq, "Pultec EQP-1A style low trick",
            "Passive program EQ: boost and attenuate the same low frequency (60 Hz) for a big but tight low end with a dip "
            "just above, a broad presence lift at 10 kHz, a gentle top attenuation, then the tube make-up and output iron.",
            [] (Graph& g) { serial (g, {
                { "tone.eq", "Program EQ", {}, -1, 0, bands ({ { 1, 60, 5.0f, 0.5f }, { 0, 220, -2.0f, 0.7f }, { 0, 10000, 3.0f, 0.5f }, { 2, 20000, -1.5f, 0.6f } }) },
                { "tube.stage", "Tube make-up", { { "drive_db", 5 }, { "bias", 0.12f }, { "character", 0.15f }, { "gain_comp", 1 } } },
                { "character.transformer", "Output iron", { { "profile", 2 }, { "drive_db", 3 }, { "core_sat", 0.45f } } } }); }));

        v.push_back (t (eq, "Neve 1073 style channel",
            "Class-A preamp and EQ: 80 Hz high-pass at 18 dB/oct, a broad 110 Hz shelf, a wide 1.6 kHz mid lift and the fixed 12 kHz "
            "high shelf - through the Marinair transformers (rich low-mid saturation).",
            [] (Graph& g) { serial (g, {
                { "character.transformer", "Input iron", { { "profile", 2 }, { "drive_db", 6 }, { "core_sat", 0.6f }, { "lf_corner_hz", 15 } } },
                { "tone.eq", "1073 EQ", {}, -1, 0, bands ({ { 3, 80, 0, 0.71f, 2 }, { 1, 110, 2.0f, 0.6f }, { 0, 1600, 2.0f, 0.9f }, { 2, 12000, 3.0f, 0.6f } }) },
                { "character.console", "Class A", { { "model", 1 }, { "drive_db", 4 } } } }); }));

        v.push_back (t (eq, "API 550A style",
            "Proportional-Q EQ: the more you boost, the narrower the band. Punchy 100 Hz, focused 1.5 kHz and 7 kHz lifts "
            "with the American console's forward midrange.",
            [] (Graph& g) { serial (g, {
                { "tone.eq", "550A", {}, -1, 0, bands ({ { 0, 100, 3.0f, 1.0f }, { 0, 1500, 2.0f, 1.2f }, { 0, 7000, 3.0f, 1.0f } }) },
                { "character.console", "API colour", { { "model", 2 }, { "drive_db", 5 } } } }); }));

        v.push_back (t (eq, "SSL E-series channel",
            "British console channel EQ: 18 dB/oct high-pass, tight low cut-and-shelf, a surgical 3 kHz presence bell and a bright "
            "top shelf - clean and punchy.",
            [] (Graph& g) { serial (g, {
                { "tone.eq", "E-channel", {}, -1, 0, bands ({ { 3, 60, 0, 0.71f, 2 }, { 1, 100, 2.0f, 0.7f }, { 0, 350, -2.0f, 1.5f },
                                                               { 0, 3000, 2.5f, 1.4f }, { 2, 10000, 2.0f, 0.7f } }) },
                { "character.console", "SSL bus", { { "model", 1 }, { "drive_db", 2 } } } }); }));

        v.push_back (t (eq, "Air band (Maag style)",
            "An 'air' shelf centred far above the audible top, so only the very top opens up - silky, without harshness.",
            [] (Graph& g) { serial (g, {
                { "tone.eq", "Air", {}, -1, 0, bands ({ { 2, 16000, 4.0f, 0.5f }, { 0, 2500, 0.8f, 0.4f } }) } }); }));

        // ================================================ tape and character
        v.push_back (t (tape, "Studer A800 style tape (15 ips)",
            "Multitrack tape at 15 ips: head bump around 50-60 Hz, gentle top-end loss, soft saturation that compresses transients, "
            "low wow and flutter, a touch of hiss.",
            [] (Graph& g) { serial (g, {
                { "character.tape", "A800 15 ips", { { "speed", 1 }, { "formulation", 1 }, { "input_db", 4 }, { "saturation", 0.55f },
                    { "head_bump", 0.6f }, { "hf_loss", 0.4f }, { "wow", 0.08f }, { "flutter", 0.1f }, { "noise_db", -84 }, { "auto_gain", 1 } } } }); }));

        v.push_back (t (tape, "Ampex ATR-102 style (30 ips mastering)",
            "Half-inch mastering at 30 ips: extended flat lows (bump lower), clean highs, very low wow/flutter, subtle saturation and glue.",
            [] (Graph& g) { serial (g, {
                { "character.tape", "ATR 30 ips", { { "speed", 2 }, { "formulation", 1 }, { "input_db", 2 }, { "saturation", 0.35f },
                    { "head_bump", 0.4f }, { "hf_loss", 0.15f }, { "wow", 0.03f }, { "flutter", 0.04f }, { "noise_db", -96 }, { "auto_gain", 1 } } } }); }));

        v.push_back (t (tape, "Cassette deck lo-fi",
            "Worn cassette: 7.5 ips-style dull top, strong wow and flutter, hiss, crunchy saturation and narrow bandwidth.",
            [] (Graph& g) { serial (g, {
                { "character.tape", "Cassette", { { "speed", 0 }, { "formulation", 0 }, { "input_db", 8 }, { "saturation", 0.75f },
                    { "head_bump", 0.3f }, { "hf_loss", 0.85f }, { "wow", 0.55f }, { "flutter", 0.5f }, { "noise_db", -52 }, { "auto_gain", 1 } } },
                { "tone.filter", "Narrow band", { { "type", 0 }, { "slope", 0 }, { "cutoff_hz", 9000 }, { "resonance", 0.1f } } } }); }));

        v.push_back (t (tape, "Tube preamp warmth",
            "A single triode stage driven gently: 2nd-harmonic warmth that grows with level, a little sag on peaks.",
            [] (Graph& g) { serial (g, {
                { "tube.stage", "Triode", { { "drive_db", 10 }, { "bias", 0.25f }, { "character", 0.0f }, { "sag", 0.35f }, { "gain_comp", 1 } } } }); }));

        v.push_back (t (tape, "Console summing (British)",
            "Bus through a British console: channel crosstalk, component tolerance between left and right, gentle drive and transformer weight.",
            [] (Graph& g) { serial (g, {
                { "character.console", "Summing", { { "model", 1 }, { "drive_db", 6 }, { "crosstalk_db", -60 }, { "tolerance", 0.5f } } },
                { "character.transformer", "Bus iron", { { "profile", 1 }, { "drive_db", 3 } } } }); }));

        // ================================================ mixing chains
        v.push_back (t (mix, "Vocal: modern pop",
            "Clean high-pass, de-mud, presence and air (with a dynamic de-ess band), fast FET peaks into an opto leveler, "
            "exciter sheen, and parallel plate + eighth-note delay.",
            [] (Graph& g)
            {
                auto ids = serial (g, {
                    { "tone.eq", "Vocal EQ", {}, -1, 0, bands ({ { 3, 90, 0, 0.71f, 3 }, { 0, 300, -2.5f, 1.2f }, { 0, 3200, 2.0f, 1.0f },
                                                                  { 0, 7000, 0.0f, 3.0f, 1, 0, true, -30, -5, 2, 80 }, { 2, 12000, 3.0f, 0.6f } }) },
                    { "dynamics.compressor", "Peaks (FET)", { { "model", fet }, { "threshold_db", -20 }, { "ratio", 4 }, { "attack_ms", 0.8f },
                        { "release_ms", 80 }, { "color", 0.3f }, { "auto_makeup", 1 } } },
                    { "dynamics.compressor", "Leveler (opto)", { { "model", opto }, { "threshold_db", -22 }, { "ratio", 3 }, { "attack_ms", 10 },
                        { "release_ms", 1200 }, { "auto_makeup", 1 } } },
                    { "tone.exciter", "Sheen", { { "freq_hz", 5000 }, { "drive_db", 6 }, { "character", 0 }, { "amount_db", -18 } } } });
                const int plate = place (g, { "space.reverb", "Plate", { { "predelay_ms", 25 }, { "size", 0.5f }, { "decay_s", 1.8f },
                                                                         { "damping_hz", 7000 }, { "lowcut_hz", 250 } }, 0.18f }, 210 + 3 * kStepX, kRowY + 150);
                const int echo = place (g, { "space.delay", "1/8 echo", { { "sync", 1 }, { "division", 5 }, { "feedback_pct", 25 },
                                                                          { "lp_hz", 5000 }, { "hp_hz", 300 }, { "ducking", 0.6f } }, 0.12f }, 210 + 3 * kStepX, kRowY - 150);
                g.connect (ids.back(), plate); g.connect (plate, Graph::kOutputId);
                g.connect (ids.back(), echo); g.connect (echo, Graph::kOutputId);
            }));

        v.push_back (t (mix, "Vocal: vintage warm",
            "Tube preamp, 1073-style lift, opto leveling and a dark tape slap: warm 60s/70s vocal.",
            [] (Graph& g) { serial (g, {
                { "tube.stage", "Preamp", { { "drive_db", 8 }, { "bias", 0.2f }, { "gain_comp", 1 } } },
                { "tone.eq", "Warm EQ", {}, -1, 0, bands ({ { 3, 80, 0, 0.71f, 2 }, { 0, 1600, 2.0f, 0.9f }, { 2, 10000, -1.5f, 0.6f } }) },
                { "dynamics.compressor", "Opto", { { "model", opto }, { "threshold_db", -24 }, { "ratio", 3 }, { "release_ms", 1500 }, { "auto_makeup", 1 } } },
                { "space.delay", "Tape slap", { { "time_ms", 110 }, { "feedback_pct", 12 }, { "lp_hz", 3500 }, { "wow", 0.25f }, { "drive", 0.4f } }, 0.18f } }); }));

        v.push_back (t (mix, "Drum bus glue",
            "VCA bus compression with a slow attack (transients through), auto release, a light tape stage and a little extra attack.",
            [] (Graph& g) { serial (g, {
                { "dynamics.compressor", "Bus glue", { { "model", vca }, { "threshold_db", -18 }, { "ratio", 2 }, { "attack_ms", 30 },
                    { "release_ms", 200 }, { "auto_release", 1 }, { "knee_db", 2 }, { "auto_makeup", 1 } } },
                { "character.tape", "Tape", { { "speed", 1 }, { "input_db", 3 }, { "saturation", 0.45f }, { "wow", 0.02f }, { "flutter", 0.03f }, { "auto_gain", 1 } } },
                { "dynamics.transient", "Snap", { { "attack_pct", 25 }, { "sustain_pct", -5 } } } }); }));

        v.push_back (t (mix, "Parallel NY drum smash",
            "New-York compression: the dry drums plus a heavily crushed copy (20:1, fast) blended underneath for density.",
            [] (Graph& g)
            {
                g.connect (Graph::kInputId, Graph::kOutputId);
                const int smash = place (g, { "dynamics.compressor", "Smash", { { "model", fet }, { "threshold_db", -38 }, { "ratio", 20 },
                                                { "attack_ms", 0.5f }, { "release_ms", 120 }, { "color", 0.6f }, { "auto_makeup", 1 } }, 1.0f, -8.0f }, 330, kRowY + 120);
                const int eqId = place (g, { "tone.eq", "Smash EQ", {}, -1, 0, bands ({ { 1, 80, 3.0f, 0.7f }, { 2, 8000, 3.0f, 0.7f } }) }, 330 + kStepX, kRowY + 120);
                g.connect (Graph::kInputId, smash); g.connect (smash, eqId); g.connect (eqId, Graph::kOutputId);
            }));

        v.push_back (t (mix, "Mix bus: gentle glue",
            "Low-ratio VCA glue, a tilt toward air, British summing and safe peaks - a polished but natural mix bus.",
            [] (Graph& g) { serial (g, {
                { "dynamics.compressor", "Glue", { { "model", vca }, { "threshold_db", -14 }, { "ratio", 1.5f }, { "attack_ms", 30 },
                    { "release_ms", 300 }, { "auto_release", 1 }, { "knee_db", 6 }, { "auto_makeup", 1 } } },
                { "tone.eq", "Tilt", {}, -1, 0, bands ({ { 7, 1000, 0.8f, 0.7f }, { 3, 25, 0, 0.71f, 3 } }) },
                { "character.console", "Summing", { { "model", 1 }, { "drive_db", 2 } } } }); }));

        v.push_back (t (mix, "808 / bass weight",
            "Warm low-band saturation for audible bass on small speakers, mono below 120 Hz, gentle peak control.",
            [] (Graph& g) { serial (g, {
                { "character.saturation", "Low harmonics", { { "character", 1 }, { "drive_db", 14 }, { "focus_lo_hz", 20 }, { "focus_hi_hz", 1000 }, { "auto_gain", 1 } }, 0.7f },
                { "utility.stereo", "Mono lows", { { "bass_mono_hz", 120 } } },
                { "dynamics.compressor", "Control", { { "model", vca }, { "threshold_db", -14 }, { "ratio", 3 }, { "attack_ms", 15 }, { "release_ms", 120 }, { "auto_makeup", 1 } } } }); }));

        v.push_back (t (mix, "Acoustic guitar sparkle",
            "Low cut, boxiness out, a wide high lift, gentle opto and a short room.",
            [] (Graph& g) { serial (g, {
                { "tone.eq", "Guitar EQ", {}, -1, 0, bands ({ { 3, 100, 0, 0.71f, 2 }, { 0, 250, -3.0f, 1.4f }, { 2, 8000, 3.0f, 0.6f } }) },
                { "dynamics.compressor", "Opto", { { "model", opto }, { "threshold_db", -20 }, { "ratio", 2.5f }, { "auto_makeup", 1 } } },
                { "space.reverb", "Small room", { { "predelay_ms", 8 }, { "size", 0.25f }, { "decay_s", 0.7f }, { "early", 0.6f } }, 0.15f } }); }));

        // ================================================ creative
        v.push_back (t (fx, "Shimmer reverb",
            "Octave-up pitch shifting with feedback, feeding a long, bright reverb - blended with the dry signal.",
            [] (Graph& g)
            {
                g.connect (Graph::kInputId, Graph::kOutputId);
                serial (g, {
                    { "pitch.shift", "Octave up", { { "semitones", 12 }, { "window_ms", 70 }, { "feedback", 0.45f }, { "tone_hz", 9000 } }, 1.0f },
                    { "space.reverb", "Cathedral", { { "predelay_ms", 40 }, { "size", 0.95f }, { "decay_s", 8 }, { "damping_hz", 9000 },
                                                     { "mod_depth", 0.5f }, { "lowcut_hz", 300 } }, 1.0f, -6.0f } }, 210, kRowY + 130);
            }));

        v.push_back (t (fx, "Octave-down sub",
            "Adds a pitched-down octave filtered to the sub range under the original.",
            [] (Graph& g)
            {
                g.connect (Graph::kInputId, Graph::kOutputId);
                serial (g, {
                    { "pitch.shift", "Octave down", { { "semitones", -12 }, { "window_ms", 60 } }, 1.0f },
                    { "tone.filter", "Sub only", { { "type", 0 }, { "slope", 1 }, { "cutoff_hz", 180 } }, 1.0f, -4.0f } }, 210, kRowY + 130);
            }));

        v.push_back (t (fx, "Wide vocal doubler",
            "Two detuned, slightly delayed copies (left down, right up) for an ADT-style wide double.",
            [] (Graph& g) { serial (g, {
                { "pitch.shift", "Doubler", { { "semitones", 0 }, { "spread_cents", 12 }, { "window_ms", 35 } }, 0.45f },
                { "utility.stereo", "Width", { { "width_pct", 140 } } } }); }));

        v.push_back (t (fx, "Telephone / radio",
            "Narrow band-pass (300 Hz - 3 kHz), gritty distortion and a mono speaker.",
            [] (Graph& g) { serial (g, {
                { "tone.eq", "Band limit", {}, -1, 0, bands ({ { 3, 300, 0, 0.71f, 3 }, { 4, 3200, 0, 0.71f, 3 }, { 0, 1500, 4.0f, 1.0f } }) },
                { "character.saturation", "Grit", { { "character", 5 }, { "drive_db", 18 }, { "auto_gain", 1 } } },
                { "utility.gain", "Mono", { { "mono", 1 } } } }); }));

        v.push_back (t (fx, "Tape echo (Space Echo style)",
            "Dotted-eighth tape echo: darkening, thinning repeats, wow, a saturated loop and gentle ducking.",
            [] (Graph& g) { serial (g, {
                { "space.delay", "Tape echo", { { "sync", 1 }, { "division", 6 }, { "feedback_pct", 48 }, { "lp_hz", 3800 }, { "hp_hz", 180 },
                                                { "wow", 0.45f }, { "drive", 0.55f }, { "ducking", 0.3f } }, 0.3f } }); }));

        v.push_back (t (fx, "Juno-style chorus",
            "Lush stereo bucket-brigade style chorus: two voices, slow rate, wide spread, a dark tone.",
            [] (Graph& g) { serial (g, {
                { "mod.chorus", "Chorus I+II", { { "voices", 1 }, { "rate_hz", 0.5f }, { "depth_ms", 3.5f }, { "delay_ms", 8 }, { "spread", 1 }, { "tone_hz", 9000 } }, 0.5f } }); }));

        v.push_back (t (fx, "Jet flanger",
            "Slow, deep, resonant flange sweep with opposite-phase sides.",
            [] (Graph& g) { serial (g, {
                { "mod.flanger", "Jet", { { "rate_hz", 0.12f }, { "depth", 0.9f }, { "delay_ms", 2.5f }, { "feedback_pct", 70 }, { "stereo_deg", 180 } }, 0.5f } }); }));

        v.push_back (t (fx, "Small Stone-style phaser",
            "Four-stage phaser with a vocal, resonant sweep.",
            [] (Graph& g) { serial (g, {
                { "mod.phaser", "Phaser", { { "stages", 1 }, { "rate_hz", 0.35f }, { "depth", 0.85f }, { "centre_hz", 900 }, { "range_oct", 3.5f },
                                            { "feedback_pct", 55 }, { "stereo_deg", 90 } }, 0.5f } }); }));

        v.push_back (t (fx, "EMT 140-style plate",
            "Bright, dense plate: short pre-delay, ~2.2 s decay, high diffusion, lows trimmed.",
            [] (Graph& g) { serial (g, {
                { "space.reverb", "Plate", { { "predelay_ms", 20 }, { "size", 0.55f }, { "decay_s", 2.2f }, { "damping_hz", 9000 },
                                             { "diffusion", 0.9f }, { "early", 0.1f }, { "lowcut_hz", 200 } }, 0.25f } }); }));

        v.push_back (t (fx, "Ambient wash",
            "Chorused, long, modulated reverb with a dark ping-pong delay feeding it.",
            [] (Graph& g) { serial (g, {
                { "space.delay", "Ping-pong", { { "sync", 1 }, { "division", 8 }, { "feedback_pct", 55 }, { "ping_pong", 1 }, { "lp_hz", 4000 } }, 0.35f },
                { "mod.chorus", "Chorus", { { "rate_hz", 0.3f }, { "depth_ms", 4 } }, 0.4f },
                { "space.reverb", "Wash", { { "predelay_ms", 60 }, { "size", 0.9f }, { "decay_s", 9 }, { "damping_hz", 5000 }, { "mod_depth", 0.7f } }, 0.5f } }); }));

        v.push_back (t (fx, "Lo-fi crunch",
            "Bit of everything broken: square-ish distortion, a resonant low-pass, cassette wobble.",
            [] (Graph& g) { serial (g, {
                { "character.saturation", "Crunch", { { "character", 3 }, { "drive_db", 20 }, { "auto_gain", 1 } } },
                { "tone.filter", "Resonant LP", { { "type", 0 }, { "slope", 1 }, { "cutoff_hz", 3500 }, { "resonance", 0.5f } } },
                { "character.tape", "Wobble", { { "speed", 0 }, { "wow", 0.6f }, { "flutter", 0.4f }, { "noise_db", -60 }, { "auto_gain", 1 } } } }); }));

        return v;
    }
}

namespace templates
{
    const std::vector<Template>& factory()
    {
        static const std::vector<Template> list = makeFactory();
        return list;
    }

    juce::File folder()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Atelier").getChildFile ("Templates");
    }

    std::vector<Template> personal()
    {
        std::vector<Template> v;
        for (auto& f : folder().findChildFiles (juce::File::findFiles, false, "*.atelier-template"))
        {
            auto xml = juce::XmlDocument::parse (f);
            if (xml == nullptr || ! xml->hasTagName ("ATELIER_TEMPLATE")) continue;
            Template x;
            x.factory = false;
            x.file = f;
            x.name = xml->getStringAttribute ("name", f.getFileNameWithoutExtension());
            x.category = xml->getStringAttribute ("category", "My templates");
            x.description = xml->getStringAttribute ("description");
            v.push_back (x);
        }
        std::sort (v.begin(), v.end(), [] (const Template& a, const Template& b) { return a.name.compareIgnoreCase (b.name) < 0; });
        return v;
    }

    juce::ValueTree bridgeFor (const Template& tpl)
    {
        if (tpl.factory)
        {
            sonic::Graph g;
            if (tpl.build) tpl.build (g);
            return AtelierProcessor::graphToTree (g);
        }
        auto xml = juce::XmlDocument::parse (tpl.file);
        if (xml == nullptr) return {};
        if (auto* b = xml->getChildByName ("BRIDGE"))
            return juce::ValueTree::fromXml (*b);
        return {};
    }

    bool save (const juce::String& name, const juce::String& category, const juce::String& description,
               const juce::ValueTree& bridge, juce::String& error)
    {
        const auto clean = juce::File::createLegalFileName (name.trim());
        if (clean.isEmpty()) { error = "Please give the template a name."; return false; }
        if (! folder().createDirectory()) { error = "Could not create " + folder().getFullPathName(); return false; }
        juce::XmlElement root ("ATELIER_TEMPLATE");
        root.setAttribute ("name", name.trim());
        root.setAttribute ("category", category.trim().isEmpty() ? juce::String ("My templates") : category.trim());
        root.setAttribute ("description", description.trim());
        root.setAttribute ("version", 1);
        if (auto x = bridge.createXml()) root.addChildElement (x.release());
        const auto file = folder().getChildFile (clean + ".atelier-template");
        if (! root.writeTo (file)) { error = "Could not write " + file.getFullPathName(); return false; }
        return true;
    }

    bool remove (const Template& tpl)
    {
        return ! tpl.factory && tpl.file.existsAsFile() && (tpl.file.moveToTrash() || tpl.file.deleteFile());
    }
}
