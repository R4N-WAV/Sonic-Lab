#include "ParamHelp.h"

#include <cctype>
#include <map>

namespace help
{
namespace
{
    struct Entry { const char* key; const char* what; const char* more; const char* less; };

    // key = "parameter id" (shared meaning) or "block type/parameter id" (block-specific).
    const Entry kEntries[] = {
        // ---- shared ----
        { "output_db", "Level leaving this block.", "louder out of the block", "quieter out of the block" },
        { "drive_db", "How hard the sound hits the block.", "more grit, harmonics and thickness", "cleaner, closer to the original" },
        { "oversampling", "Runs the colouring at a higher internal rate.", "smoother, purer top end, more CPU", "less CPU; harsh highs can get a gritty edge" },
        { "auto_gain", "Keeps the volume steady while you change the drive.", "", "" },
        { "bias", "Shifts the circuit's resting point.", "more lopsided, warmer even harmonics", "more symmetric and neutral" },
        { "character", "The kind of colour this block adds.", "", "" },
        { "wow", "Slow pitch wobble, like an old tape machine.", "more seasick drift", "steadier pitch" },
        { "feedback_pct", "How much of the effect is fed back into itself.", "more repeats, ringing and resonance", "cleaner, shorter effect" },
        { "rate_hz", "Speed of the movement.", "faster wobble", "slower, lazier movement" },
        { "width", "Stereo width of the effect.", "wider, spills to the sides", "narrower, closer to mono" },
        { "invert_l", "Flips the left channel upside down (polarity).", "", "" },
        { "invert_r", "Flips the right channel upside down (polarity).", "", "" },

        // ---- Tube ----
        { "tube.stage/drive_db", "How hard you push the tube.", "thicker, warmer, more harmonics and squash on peaks", "cleaner and more open" },
        { "tube.stage/character", "Triode (left) to pentode (right).", "harder, buzzier, more odd harmonics", "softer, sweeter, more 2nd harmonic" },
        { "tube.stage/sag", "The tube 'breathing' on loud peaks.", "more bloom and soft compression on hits", "tighter, steadier response" },
        { "tube.stage/hf_rolloff_hz", "Darkens the sound before it saturates.", "brighter, crisper saturation", "darker, smoother saturation" },
        { "tube.stage/gain_comp", "Unity keeps quiet parts at the same level whatever the drive.", "", "" },

        // ---- Tape ----
        { "character.tape/speed", "Tape speed. 15 ips is the classic studio sound.", "faster: tighter lows, more extended highs", "slower: fatter low bump, darker top, more colour" },
        { "character.tape/formulation", "Type of tape: Vintage saturates early, Hot takes the most level.", "", "" },
        { "character.tape/input_db", "How hard you hit the tape.", "more compression, saturation and glue", "cleaner and more open" },
        { "character.tape/bias", "Recording bias of the machine.", "cleaner and slightly darker", "more distortion and a brighter edge" },
        { "character.tape/saturation", "How much the tape holds on to the sound (hysteresis).", "thicker, rounder, more smeared transients", "more transparent" },
        { "character.tape/head_bump", "The low-end bump of the playback head.", "fatter, rounder bass", "flatter, tighter bass" },
        { "character.tape/hf_loss", "How much top end the tape loses.", "darker, softer top", "brighter, more open top" },
        { "character.tape/flutter", "Fast pitch flutter.", "more warble and grit in sustained notes", "steadier, cleaner tone" },
        { "character.tape/noise_db", "Tape hiss.", "more audible hiss", "quieter; -120 is off" },
        { "character.tape/emphasis", "Machine pre-emphasis: highs saturate earlier, like real tape.", "", "" },

        // ---- Transformer ----
        { "character.transformer/profile", "Core metal: nickel is clean, iron is vintage and thick.", "", "" },
        { "character.transformer/drive_db", "Level into the transformer.", "thicker low end, more harmonics", "cleaner" },
        { "character.transformer/core_sat", "How easily the core saturates (it hits the bass first).", "fatter, more saturated lows", "cleaner, tighter lows" },
        { "character.transformer/lf_corner_hz", "Where the deepest bass starts to fall away.", "less sub, tighter", "deeper, fuller sub" },
        { "character.transformer/hf_res_hz", "Where the little high-frequency lift sits.", "lift moves higher, more 'air'", "lift moves lower, more presence" },
        { "character.transformer/hf_res_db", "Size of that high-frequency lift.", "more sheen and sparkle", "flatter top" },
        { "character.transformer/hf_lp_hz", "Where the very top end is rolled off.", "more extended highs", "softer, darker top" },

        // ---- Console ----
        { "character.console/model", "Channel voicing: British is silky on top and firm in the lows, American has forward mids and punch.", "", "" },
        { "character.console/drive_db", "Level into the console amplifiers.", "more glue and harmonics", "cleaner" },
        { "character.console/crosstalk_db", "Bleed between left and right, like a real console.", "more blend, slightly narrower", "more separation" },
        { "character.console/noise_db", "Console noise floor.", "more audible hiss", "quieter; -120 is off" },
        { "character.console/tolerance", "Small differences between left and right parts.", "more analogue-like L/R difference", "perfectly matched channels" },
        { "character.console/seed", "Which 'unit' the part differences come from. Try a few.", "", "" },

        // ---- Saturation ----
        { "character.saturation/character", "Harmonic recipe. Picking one loads its harmonics into the designer.", "", "" },
        { "character.saturation/drive_db", "How hard the sound hits the shaper.", "more harmonics and density", "cleaner, subtler colour" },
        { "character.saturation/even_odd", "Tilts the recipe between even and odd harmonics.", "sweeter, warmer (even)", "harder, edgier (odd)" },
        { "character.saturation/decay", "Fades out the upper harmonics.", "smoother, darker colour", "brighter, buzzier colour" },
        { "character.saturation/level_dep", "Whether harmonics grow when you play louder.", "louder parts get dirtier (natural)", "same colour at any level" },
        { "character.saturation/softness", "How gently the shaper starts to bend.", "smoother onset, rounder", "harder edge, more bite" },
        { "character.saturation/bias", "Makes the curve lopsided.", "more even harmonics that grow with level", "symmetric" },
        { "character.saturation/focus_lo_hz", "Lows below this stay clean.", "cleaner, tighter bass", "bass gets saturated too" },
        { "character.saturation/focus_hi_hz", "Highs above this stay clean.", "highs get saturated too", "cleaner, less harsh top" },
        { "character.saturation/tilt_db", "Tone after the saturation, pivoting at 1 kHz.", "brighter", "darker" },
        { "character.saturation/lp_hz", "Smooths the top after saturation.", "more open top", "softer, less fizzy" },
        { "character.saturation/stereo", "Saturate left/right, or centre and sides separately.", "", "" },
        { "harmonic", "Level of this harmonic for a full-scale sine. -90 is off.", "more of this overtone", "less of it" },

        // ---- Compressor ----
        { "dynamics.compressor/model", "Circuit: Opto is smooth, FET fast and aggressive, VCA clean and punchy, Vari-Mu thick and gentle, Diode bridge gritty.", "", "" },
        { "dynamics.compressor/threshold_db", "Level where compression starts.", "(higher) less compression, more natural", "(lower) more compression, more squash" },
        { "dynamics.compressor/ratio", "How strongly it pushes loud parts down.", "flatter, more controlled, towards limiting", "gentler, more natural" },
        { "dynamics.compressor/attack_ms", "How fast it grabs a loud hit.", "slower: more punch lets through", "faster: tames peaks, softer hits" },
        { "dynamics.compressor/release_ms", "How fast it lets go.", "slower: smoother, can pump less but sound duller", "faster: livelier, can pump or distort" },
        { "dynamics.compressor/auto_release", "Release that adapts: fast after hits, slow on sustained parts.", "", "" },
        { "dynamics.compressor/knee_db", "How gradually compression starts around the threshold.", "softer, more transparent onset", "harder, more obvious grab" },
        { "dynamics.compressor/makeup_db", "Gain added after compression.", "louder", "quieter" },
        { "dynamics.compressor/auto_makeup", "Adds back roughly the level the compressor takes away.", "", "" },
        { "dynamics.compressor/detector", "Peak reacts to every hit; RMS to the average loudness (smoother).", "", "" },
        { "dynamics.compressor/sc_hpf_hz", "Stops the bass from triggering the compression.", "bass pumps less, mids/highs control it", "bass drives the compression" },
        { "dynamics.compressor/link_pct", "Same gain change on both sides.", "steadier stereo image", "each side compresses on its own (wider, can wander)" },
        { "dynamics.compressor/program_dep", "How much the timing adapts to the material.", "more 'alive', vintage behaviour", "more predictable timing" },
        { "dynamics.compressor/color", "Harmonics from the gain element when it works hard.", "more character and grit when squashing", "cleaner gain reduction" },
        { "dynamics.compressor/key", "What it listens to. Sidechain = Atelier's sidechain input (e.g. a kick ducking the bass).", "", "" },

        // ---- Transient ----
        { "dynamics.transient/attack_pct", "The start of each hit.", "snappier, punchier hits", "softer, rounder hits" },
        { "dynamics.transient/sustain_pct", "The tail after each hit.", "longer, roomier tails", "shorter, drier, tighter" },
        { "dynamics.transient/attack_time_ms", "How long the 'attack' part of a hit lasts.", "more of the hit counts as attack", "only the very first click" },
        { "dynamics.transient/release_ms", "How long the 'sustain' part is followed.", "longer tails are shaped", "only short tails are shaped" },

        // ---- EQ ----
        { "tone.eq/phase_mode", "Zero latency = classic. Natural = smoother top end. Linear = no phase shift, adds latency.", "", "" },
        { "tone.eq/linear_quality", "Filter length in Linear mode.", "more accurate bass, more latency", "less latency, less precise bass" },
        { "tone.eq/output_db", "Overall level after the EQ.", "louder", "quieter" },
        { "band_on", "Turns this band on or off.", "", "" },
        { "band_shape", "Bell, shelf, cut, notch, band pass or tilt.", "", "" },
        { "band_freq_hz", "Which frequency the band works on.", "higher: brighter area", "lower: deeper area" },
        { "band_gain_db", "How much the band boosts or cuts.", "boost", "cut" },
        { "band_q", "How narrow the band is.", "narrower, more surgical", "wider, more musical" },
        { "band_slope", "Steepness of a cut.", "steeper, sharper cut", "gentler cut" },
        { "band_place", "Which part of the stereo image the band touches.", "", "" },
        { "band_dyn", "Makes the band react to the level in its own range.", "", "" },
        { "band_dyn_thr_db", "Level in the band where the dynamic gain starts to move.", "(higher) reacts only to loud moments", "(lower) reacts more often" },
        { "band_dyn_range_db", "Extra boost (+) or cut (-) when the band gets loud.", "stronger reaction", "subtler reaction" },
        { "band_dyn_att_ms", "How fast the dynamic band reacts.", "slower, smoother", "faster, catches short peaks" },
        { "band_dyn_rel_ms", "How fast it returns.", "slower, smoother", "faster, more lively" },

        // ---- Filter ----
        { "tone.filter/type", "Low-pass keeps lows, high-pass keeps highs, band-pass keeps a slice, notch removes one.", "", "" },
        { "tone.filter/slope", "Steepness of the filter.", "", "" },
        { "tone.filter/cutoff_hz", "Where the filter starts working.", "higher: more highs pass (low-pass)", "lower: darker (low-pass)" },
        { "tone.filter/resonance", "A peak right at the cutoff.", "more whistle and focus at the cutoff", "smooth, flat edge" },
        { "tone.filter/drive_db", "Saturation before the filter.", "grittier, louder resonance", "clean" },
        { "tone.filter/lfo_rate_hz", "Speed of the automatic cutoff sweep. 0 = off.", "faster sweeping", "slower, or off at 0" },
        { "tone.filter/lfo_depth_oct", "How far the cutoff sweeps.", "bigger sweep", "subtler sweep" },
        { "tone.filter/env_oct", "Cutoff follows how loud you play (auto-wah).", "opens on loud notes", "closes on loud notes (negative)" },

        // ---- Exciter ----
        { "tone.exciter/freq_hz", "Only sound above this gets new harmonics.", "only the very top sparkles", "more of the upper mids get excited" },
        { "tone.exciter/drive_db", "How hard the top band is driven.", "denser, crunchier sparkle", "cleaner sparkle" },
        { "tone.exciter/character", "Even = sweet and silky, Odd = crisp and edgy.", "", "" },
        { "tone.exciter/amount_db", "How much of the new harmonics is added.", "brighter, more 'expensive' air", "subtler" },

        // ---- Delay ----
        { "space.delay/time_ms", "Time between echoes.", "longer gaps between repeats", "shorter, slapback or doubling" },
        { "space.delay/sync", "Locks the delay to the song tempo.", "", "" },
        { "space.delay/division", "Note length of the echo when synced.", "", "" },
        { "space.delay/feedback_pct", "How many echoes you get.", "more and longer repeats (above 100 % it builds up)", "fewer repeats" },
        { "space.delay/lp_hz", "Darkens each repeat.", "brighter repeats", "darker, more distant repeats" },
        { "space.delay/hp_hz", "Thins out each repeat.", "thinner repeats, less mud", "fuller repeats" },
        { "space.delay/ping_pong", "Echoes bounce between left and right.", "", "" },
        { "space.delay/offset_ms", "Delays the right side a little more or less.", "wider echoes", "centred echoes" },
        { "space.delay/drive", "Saturation inside the echo loop.", "warmer, dirtier repeats", "clean repeats" },
        { "space.delay/ducking", "Pushes the echoes down while you play.", "clearer while playing, echoes bloom in gaps", "echoes always at full level" },

        // ---- Reverb ----
        { "space.reverb/predelay_ms", "Gap before the reverb starts.", "more separation, the dry sound stays clear", "reverb sits right on the sound" },
        { "space.reverb/size", "Size of the room.", "bigger, more spacious", "smaller, tighter" },
        { "space.reverb/decay_s", "How long the tail lasts.", "longer tail", "shorter tail" },
        { "space.reverb/damping_hz", "Highs above this fade faster.", "brighter tail", "darker, warmer tail" },
        { "space.reverb/low_mult", "How long the lows ring compared with the mids.", "boomier low tail", "tighter low tail" },
        { "space.reverb/diffusion", "How dense the build-up is.", "smoother, denser", "grainier, more echo-like" },
        { "space.reverb/mod_depth", "Gentle movement in the tail.", "lusher, less metallic", "steadier, more static" },
        { "space.reverb/mod_rate", "Speed of that movement.", "faster shimmer", "slower drift" },
        { "space.reverb/early", "The first distinct reflections.", "more sense of a real room", "smoother, more 'plate'" },
        { "space.reverb/lowcut_hz", "Keeps low end out of the reverb.", "cleaner, less muddy", "fuller, warmer reverb" },

        // ---- Chorus / Flanger / Phaser ----
        { "mod.chorus/voices", "Number of copies per side.", "thicker, more ensemble", "simpler, more subtle" },
        { "mod.chorus/depth_ms", "How far the copies drift.", "more wobble and detune", "subtler shimmer" },
        { "mod.chorus/delay_ms", "Base delay of the copies.", "more doubled, slapback-like", "tighter, closer to flanging" },
        { "mod.chorus/feedback", "Feeds the copies back.", "more metallic", "cleaner" },
        { "mod.chorus/spread", "How wide the copies are spread.", "wider", "narrower" },
        { "mod.chorus/tone_hz", "Darkens the copies.", "brighter chorus", "darker, softer chorus" },
        { "mod.flanger/depth", "How far the sweep goes.", "bigger 'jet' sweep", "subtler movement" },
        { "mod.flanger/delay_ms", "Centre of the sweep.", "lower, slower-sounding comb", "higher, more metallic comb" },
        { "mod.flanger/feedback_pct", "Resonance of the comb (negative sounds hollow).", "more intense, whistling", "softer" },
        { "mod.flanger/stereo_deg", "Offset between the left and right sweep.", "wider, swirling", "both sides move together" },
        { "mod.phaser/stages", "Number of filter stages (notches = stages / 2).", "more notches, deeper swirl", "simpler, gentler" },
        { "mod.phaser/depth", "How far the sweep goes.", "bigger sweep", "subtler" },
        { "mod.phaser/centre_hz", "Middle of the sweep.", "higher, airier", "lower, throatier" },
        { "mod.phaser/range_oct", "Width of the sweep.", "wider sweep", "narrower sweep" },
        { "mod.phaser/feedback_pct", "Sharpness of the notches.", "more resonant, vocal", "smoother" },
        { "mod.phaser/stereo_deg", "Offset between left and right.", "wider", "both sides together" },

        // ---- Pitch ----
        { "pitch.shift/semitones", "Transposes by semitones (12 = an octave).", "higher pitch", "lower pitch" },
        { "pitch.shift/cents", "Fine tuning (100 cents = 1 semitone).", "slightly sharper", "slightly flatter" },
        { "pitch.shift/window_ms", "Grain length of the shifter.", "smoother, a bit smeared", "tighter timing, a bit buzzy" },
        { "pitch.shift/spread_cents", "Left down and right up by this amount: a wide doubler.", "wider, thicker", "centred" },
        { "pitch.shift/feedback", "Feeds the shifted sound back to be shifted again.", "cascading shimmer", "single shift" },
        { "pitch.shift/tone_hz", "Darkens the shifted sound.", "brighter", "darker, softer" },

        // ---- Stereo ----
        { "utility.stereo/width_pct", "Stereo width. 0 = mono, 100 = unchanged.", "wider", "narrower, towards mono" },
        { "utility.stereo/mid_db", "Level of the centre.", "louder centre (vocal, kick, bass)", "quieter centre" },
        { "utility.stereo/side_db", "Level of the sides.", "wider, more ambience", "narrower" },
        { "utility.stereo/pan", "Left / right balance.", "towards the right", "towards the left" },
        { "utility.stereo/bass_mono_hz", "Everything below this becomes mono.", "more of the low end is mono (tighter)", "keeps the bass stereo" },
        { "utility.stereo/rotate_deg", "Rotates the stereo image.", "image leans right", "image leans left" },
        { "utility.stereo/swap", "Swaps left and right.", "", "" },

        // ---- Utility ----
        { "utility.gain/gain_db", "Level change.", "louder", "quieter" },
        { "utility.gain/mono", "Sums left and right to mono.", "", "" },
    };

    const std::map<std::string, const Entry*>& table()
    {
        static const auto t = []
        {
            std::map<std::string, const Entry*> m;
            for (auto& e : kEntries)
                m[e.key] = &e;
            return m;
        }();
        return t;
    }

    std::string normalise (const std::string& id)
    {
        // EQ bands: b12_freq_hz -> band_freq_hz;  harmonics: h3_dbc -> harmonic
        if (id.size() > 2 && id[0] == 'b' && std::isdigit ((unsigned char) id[1]))
        {
            const auto u = id.find ('_');
            if (u != std::string::npos) return "band" + id.substr (u);
        }
        if (id.size() > 2 && id[0] == 'h' && std::isdigit ((unsigned char) id[1]) && id.find ("_dbc") != std::string::npos)
            return "harmonic";
        return id;
    }
}

Text forParam (const std::string& type, const std::string& id)
{
    const auto& t = table();
    const auto key = normalise (id);
    auto it = t.find (type + "/" + key);
    if (it == t.end()) it = t.find (key);
    if (it == t.end()) return {};
    return { juce::String (it->second->what), juce::String (it->second->more), juce::String (it->second->less) };
}

juce::String tooltip (const juce::String& name, const Text& t)
{
    juce::String s = name;
    if (t.what.isNotEmpty()) s << ": " << t.what;
    if (t.more.isNotEmpty()) s << "\nMore = " << t.more;
    if (t.less.isNotEmpty()) s << "\nLess = " << t.less;
    return s;
}

juce::String tooltip (const sonic::Module& m, int index)
{
    const auto& s = m.params()[(size_t) index];
    auto t = forParam (m.typeId(), s.id);
    if (t.empty()) t.what = juce::String (s.description);
    juce::String tip = tooltip (juce::String (s.name), t);
    if (! m.isParamRelevant (index))
    {
        const auto why = m.irrelevantReason (index);
        tip << "\n\n" << (why.empty() ? juce::String ("Not used with the current settings.") : juce::String (why));
    }
    if (! s.isChoice())
        tip << "\n\nDouble-click: reset.  Right-click or click the number: type a value.";
    return tip;
}

} // namespace help
