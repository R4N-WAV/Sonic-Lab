#include "Blueprint.h"
#include "Measure.h"
#include "PdfWriter.h"

#include <sonic/Analysis.h>
#include <sonic/ModuleRegistry.h>
#include <sonic/TransferAnalyzer.h>
#include <sonic/modules/CompressorModule.h>
#include <sonic/modules/ParametricEqModule.h>
#include <sonic/modules/SaturationModule.h>
#include <sonic/modules/TubeModule.h>

namespace Blueprint
{

namespace
{
    const char* kSchemaId = "atelier.blueprint/1.0";

    // ------------------------------------------------------------------ helpers

    juce::String safeName (const juce::String& s)
    {
        auto t = s.trim().replaceCharacters (" /\\:*?\"<>|", "__________").retainCharacters (
            "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-.");
        return t.isEmpty() ? "Untitled" : t.substring (0, 60);
    }

    bool writeWav (const juce::File& f, double fs, const std::vector<float>& l, const std::vector<float>& r)
    {
        f.deleteFile();
        juce::WavAudioFormat wav;
        auto stream = f.createOutputStream();
        if (stream == nullptr) return false;
        std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (stream.release(), fs, 2, 24, {}, 0));
        if (w == nullptr) return false;
        const float* ch[2] = { l.data(), r.data() };
        return w->writeFromFloatArrays (ch, 2, (int) l.size());
    }

    bool writeText (const juce::File& f, const juce::String& s) { return f.replaceWithText (s, false, false, "\n"); }

    // "0.9 ms", or "< 1.0 ms" when below what the method can resolve, or "n/a".
    juce::String timingText (double ms, double resolution)
    {
        if (ms < 0) return "n/a";
        if (ms < resolution) return "< " + juce::String (resolution, 1) + " ms";
        return juce::String (ms, ms < 10 ? 1 : 0) + " ms";
    }

    juce::String fmtNum (double v, int dp = 2)
    {
        if (! std::isfinite (v)) return "null";
        return juce::String (v, dp);
    }

    juce::var obj() { return juce::var (new juce::DynamicObject()); }
    void set (juce::var& o, const char* k, const juce::var& v) { o.getDynamicObject()->setProperty (k, v); }

    juce::String typeName (const std::string& t)
    {
        for (auto& m : sonic::availableModules())
            if (m.typeId == t) return m.displayName;
        return t;
    }

    // ------------------------------------------------------------------ plotting

    struct Series
    {
        juce::String name;
        juce::Colour colour;
        std::vector<double> x, y;
        bool dashed = false, bars = false;
    };

    struct PlotSpec
    {
        juce::String title, subtitle, xLabel, yLabel;
        bool logX = true;
        double x0 = 20, x1 = 20000, y0 = -24, y1 = 24;
        std::vector<Series> series;
        juce::StringArray notes;
    };

    const juce::Colour cBg { 0xff0c1117 }, cGrid { 0xff1c2632 }, cText { 0xffc6d0dc }, cDim { 0xff8796aa },
                       cBlue { 0xff86a3cc }, cBeige { 0xffc9b28a }, cGrey { 0xff6f8095 }, cGreen { 0xff7fb08f };

    juce::Image renderPlot (const PlotSpec& p)
    {
        juce::Image img (juce::Image::RGB, 1600, 900, true);
        juce::Graphics g (img);
        g.fillAll (cBg);
        g.setColour (cText);
        g.setFont (theme::sansSemi (34.0f));
        g.drawText (p.title, 70, 28, 1460, 40, juce::Justification::centredLeft);
        g.setColour (cDim);
        g.setFont (theme::sans (20.0f));
        g.drawText (p.subtitle, 70, 70, 1460, 26, juce::Justification::centredLeft);

        const juce::Rectangle<float> r (120.0f, 130.0f, 1420.0f, 640.0f);
        auto xmap = [&] (double x) { return p.logX ? r.getX() + (float) (std::log (x / p.x0) / std::log (p.x1 / p.x0)) * r.getWidth()
                                                   : r.getX() + (float) ((x - p.x0) / (p.x1 - p.x0)) * r.getWidth(); };
        auto ymap = [&] (double y) { return r.getBottom() - (float) ((y - p.y0) / (p.y1 - p.y0)) * r.getHeight(); };

        g.setFont (theme::mono (17.0f));
        if (p.logX)
            for (double f : { 20.0, 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 20000.0 })
            {
                if (f < p.x0 || f > p.x1) continue;
                const float x = xmap (f);
                g.setColour (cGrid); g.drawVerticalLine ((int) x, r.getY(), r.getBottom());
                g.setColour (cDim);
                g.drawText (f >= 1000 ? juce::String ((int) (f / 1000)) + "k" : juce::String ((int) f), (int) x - 40, (int) r.getBottom() + 8, 80, 22, juce::Justification::centred);
            }
        else
            for (int i = 0; i <= 10; ++i)
            {
                const double v = p.x0 + (p.x1 - p.x0) * i / 10.0;
                const float x = xmap (v);
                g.setColour (cGrid); g.drawVerticalLine ((int) x, r.getY(), r.getBottom());
                g.setColour (cDim);
                g.drawText (juce::String (v, std::abs (p.x1 - p.x0) < 20 ? 1 : 0), (int) x - 50, (int) r.getBottom() + 8, 100, 22, juce::Justification::centred);
            }
        for (int i = 0; i <= 8; ++i)
        {
            const double v = p.y0 + (p.y1 - p.y0) * i / 8.0;
            const float y = ymap (v);
            g.setColour (std::abs (v) < 1e-9 ? cGrey : cGrid);
            g.drawHorizontalLine ((int) y, r.getX(), r.getRight());
            g.setColour (cDim);
            g.drawText (juce::String (v, std::abs (p.y1 - p.y0) < 8 ? 2 : 0), 10, (int) y - 11, 100, 22, juce::Justification::centredRight);
        }
        g.setColour (cDim);
        g.setFont (theme::sans (19.0f));
        g.drawText (p.xLabel, r.toNearestInt().withY ((int) r.getBottom() + 34).withHeight (26), juce::Justification::centred);
        g.saveState();
        g.addTransform (juce::AffineTransform::rotation (-juce::MathConstants<float>::halfPi, 30.0f, r.getCentreY()));
        g.drawText (p.yLabel, juce::Rectangle<int> (30 - 300, (int) r.getCentreY() - 13, 600, 26), juce::Justification::centred);
        g.restoreState();

        g.saveState();
        g.reduceClipRegion (r.toNearestInt());
        for (auto& s : p.series)
        {
            if (s.bars)
            {
                g.setColour (s.colour);
                for (size_t i = 0; i < s.x.size(); ++i)
                {
                    const float x = xmap (s.x[i]);
                    const float y = ymap (juce::jlimit (p.y0, p.y1, s.y[i]));
                    g.fillRect (x - 14.0f, y, 28.0f, r.getBottom() - y);
                }
                continue;
            }
            juce::Path path;
            bool started = false;
            for (size_t i = 0; i < s.x.size(); ++i)
            {
                if (! std::isfinite (s.y[i]) || s.y[i] < -190.0) { started = false; continue; }
                const float x = xmap (s.x[i]), y = ymap (juce::jlimit (p.y0 - (p.y1 - p.y0), p.y1 + (p.y1 - p.y0), s.y[i]));
                if (! started) { path.startNewSubPath (x, y); started = true; } else path.lineTo (x, y);
            }
            g.setColour (s.colour);
            if (s.dashed)
            {
                juce::Path d;
                const float dash[] = { 10.0f, 7.0f };
                juce::PathStrokeType (2.5f).createDashedStroke (d, path, dash, 2);
                g.fillPath (d);
            }
            else
                g.strokePath (path, juce::PathStrokeType (3.0f));
        }
        g.restoreState();
        g.setColour (cGrid);
        g.drawRect (r, 1.5f);

        // legend + notes
        int lx = 130, ly = 140;
        g.setFont (theme::sans (19.0f));
        for (auto& s : p.series)
        {
            if (s.name.isEmpty()) continue;
            g.setColour (s.colour);
            g.fillRect (lx, ly + 10, 26, 4);
            g.setColour (cText);
            g.drawText (s.name, lx + 34, ly, 600, 24, juce::Justification::centredLeft);
            ly += 28;
        }
        g.setColour (cDim);
        g.setFont (theme::mono (16.0f));
        int ny = 812;
        for (auto& n : p.notes)
        {
            g.drawText (n, 70, ny, 1460, 22, juce::Justification::centredLeft);
            ny += 22;
        }
        g.setColour (cBeige);
        g.setFont (theme::serif (26.0f));
        g.drawText ("Atelier", 1400, 26, 140, 40, juce::Justification::centredRight);
        return img;
    }

    bool savePng (const juce::Image& img, const juce::File& f)
    {
        f.deleteFile();
        juce::FileOutputStream out (f);
        if (! out.openedOk()) return false;
        juce::PNGImageFormat png;
        return png.writeImageToStream (img, out);
    }

    // ------------------------------------------------------------------ bridge introspection

    struct BlockInfo
    {
        int id = 0;
        int stage = 0;          // 1-based position in processing order (0 = not in the signal path)
        std::string type;
        juce::String name, topology;
        bool enabled = true, active = false;
        float mix = 1, outDb = 0, x = 0, y = 0;
        int latency = 0;
        struct P { juce::String id, name, unit, group, desc, valueText; double value, min, max, def; juce::StringArray choices; bool active = true; juce::String inactiveReason; };
        std::vector<P> params;
        // isolated measurements
        double thd18 = -200, thd6 = -200, h2_18 = -200, h3_18 = -200, gain18 = 0;
        std::vector<double> frF, frDb;
        std::vector<double> staticX, staticY; // transfer / compression / eq curves
        juce::String staticKind;
    };

    juce::String stageLabel (const BlockInfo& b) { return b.stage > 0 ? juce::String (b.stage).paddedLeft ('0', 2) : juce::String ("--"); }

    std::vector<BlockInfo> describe (const juce::ValueTree& tree, double fs, std::vector<int>& order, std::vector<sonic::Graph::Edge>& edges)
    {
        sonic::Graph g;
        AtelierProcessor::treeToGraph (tree, g);
        g.prepare (fs, 512);
        order = g.activeOrder();
        edges = g.edges();
        std::vector<BlockInfo> out;
        for (int id : g.nodeIds())
        {
            auto* n = g.node (id);
            BlockInfo b;
            b.id = id;
            b.type = n->type;
            b.name = n->name;
            b.topology = n->module->topology();
            b.enabled = n->enabled.load();
            const auto it = std::find (order.begin(), order.end(), id);
            b.active = it != order.end();
            b.stage = b.active ? (int) (it - order.begin()) + 1 : 0;
            b.mix = n->mix.load();
            b.outDb = n->outGainDb.load();
            b.x = n->x; b.y = n->y;
            b.latency = n->module->latencySamples();
            auto& m = *n->module;
            for (int i = 0; i < m.numParams(); ++i)
            {
                const auto& s = m.params()[(size_t) i];
                BlockInfo::P p { s.id, s.name, s.unit, s.group, s.description, formatParam (s, m.getParam (i)),
                                 m.getParam (i), s.minValue, s.maxValue, s.defaultValue, {} };
                for (auto& c : s.choices) p.choices.add (c);
                p.active = m.isParamRelevant (i);
                p.inactiveReason = m.irrelevantReason (i);
                b.params.push_back (p);
            }
            // static characteristics computed from the same DSP code
            if (n->type == "tone.eq")
            {
                auto& eq = static_cast<sonic::ParametricEqModule&> (m);
                b.staticKind = "magnitude_db_vs_hz";
                for (int i = 0; i <= 200; ++i)
                {
                    const double f = 20.0 * std::pow (1000.0, i / 200.0);
                    b.staticX.push_back (f);
                    b.staticY.push_back (eq.responseDb (f));
                }
            }
            else if (n->type == "dynamics.compressor")
            {
                auto& c = static_cast<sonic::CompressorModule&> (m);
                b.staticKind = "output_db_vs_input_db";
                for (int i = 0; i <= 60; ++i) { b.staticX.push_back (-60.0 + i); b.staticY.push_back (c.staticCurveDb (-60.0 + i)); }
            }
            else if (n->type == "character.saturation" || n->type == "tube.stage")
            {
                b.staticKind = "output_vs_input_fullscale";
                for (int i = 0; i <= 200; ++i)
                {
                    const double x = -1.0 + i / 100.0;
                    b.staticX.push_back (x);
                    b.staticY.push_back (n->type == "tube.stage" ? static_cast<sonic::TubeModule&> (m).staticCurve (x)
                                                                 : static_cast<sonic::SaturationModule&> (m).staticCurve (x));
                }
            }
            out.push_back (std::move (b));
        }
        return out;
    }

    void measureBlock (BlockInfo& b, const juce::ValueTree& tree, double fs)
    {
        const auto iso = measure::isolateBlock (tree, b.id);
        std::vector<float> oL, oR;
        for (double lvl : { -18.0, -6.0 })
        {
            std::vector<float> x ((size_t) (1.2 * fs));
            const double a = std::pow (10.0, lvl / 20.0);
            for (size_t i = 0; i < x.size(); ++i) x[i] = (float) (a * std::sin (sonic::kTwoPi * 1000.0 * i / fs));
            measure::render (iso, fs, x, x, oL, oR);
            auto sp = sonic::computeSpectrum (oL.data(), (int) oL.size(), 16384, fs, sonic::WindowType::blackmanHarris4);
            auto h = sonic::analyseHarmonics (sp, 1000.0, 10, std::min (20000.0, fs * 0.45));
            if (! h.valid) continue;
            if (lvl < -10) { b.thd18 = h.thdDb; b.h2_18 = h.levelDbc[1]; b.h3_18 = h.numHarmonics > 2 ? h.levelDbc[2] : -200; b.gain18 = h.levelDbfs[0] - lvl; }
            else b.thd6 = h.thdDb;
        }
        sonic::Noise rng (0x77);
        std::vector<float> x ((size_t) (3 * fs));
        for (auto& v : x) v = (float) (0.125 * std::sqrt (3.0) * rng.next());
        measure::render (iso, fs, x, x, oL, oR);
        sonic::TransferAnalyzer ta;
        ta.prepare (fs, 8192, 0.0);
        ta.addBlock (x.data() + 8192, oL.data() + 8192, (int) x.size() - 8192);
        auto t = ta.result();
        for (double f : { 31.5, 63.0, 125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0, 16000.0 })
        {
            if (f > fs * 0.45 || ! t.valid) continue;
            const int k = (int) std::lround (f / t.binHz);
            b.frF.push_back (f);
            b.frDb.push_back (t.magnitudeDb[(size_t) k]);
        }
    }

    juce::Image drawChain (const std::vector<BlockInfo>& blocks, const std::vector<sonic::Graph::Edge>& edges, const juce::String& title)
    {
        juce::Image img (juce::Image::RGB, 1600, 900, true);
        juce::Graphics g (img);
        g.fillAll (cBg);
        g.setColour (cText);
        g.setFont (theme::sansSemi (34.0f));
        g.drawText ("Signal chain architecture", 70, 28, 1400, 40, juce::Justification::centredLeft);
        g.setColour (cDim);
        g.setFont (theme::sans (20.0f));
        g.drawText (title + "  -  parallel paths are summed; all paths are latency-aligned", 70, 70, 1460, 26, juce::Justification::centredLeft);

        // bounding box of block positions -> fit into the drawing area
        float minX = 1e9f, maxX = -1e9f, minY = 1e9f, maxY = -1e9f;
        for (auto& b : blocks) { minX = juce::jmin (minX, b.x); maxX = juce::jmax (maxX, b.x + 188.0f); minY = juce::jmin (minY, b.y); maxY = juce::jmax (maxY, b.y + 96.0f); }
        if (blocks.empty()) { minX = 0; maxX = 400; minY = 0; maxY = 200; }
        const juce::Rectangle<float> area (300.0f, 150.0f, 1000.0f, 640.0f);
        const float sc = juce::jmin (1.4f, area.getWidth() / juce::jmax (1.0f, maxX - minX), area.getHeight() / juce::jmax (1.0f, maxY - minY));
        auto boxOf = [&] (int id) -> juce::Rectangle<float>
        {
            if (id == 0) return { 60.0f, 400.0f, 180.0f, 110.0f };
            if (id == 1) return { 1360.0f, 400.0f, 180.0f, 110.0f };
            for (auto& b : blocks)
                if (b.id == id)
                    return { area.getX() + (b.x - minX) * sc, area.getY() + (b.y - minY) * sc, 188.0f * juce::jmin (1.0f, sc * 1.1f), 96.0f * juce::jmin (1.0f, sc * 1.1f) };
            return {};
        };
        for (auto& e : edges)
        {
            const auto a = boxOf (e.from), b = boxOf (e.to);
            juce::Path p;
            const juce::Point<float> s (a.getRight(), a.getCentreY()), t (b.getX(), b.getCentreY());
            const float dx = juce::jmax (50.0f, std::abs (t.x - s.x) * 0.45f);
            p.startNewSubPath (s);
            p.cubicTo (s.x + dx, s.y, t.x - dx, t.y, t.x, t.y);
            g.setColour (cText.withAlpha (0.8f));
            g.strokePath (p, juce::PathStrokeType (2.5f));
        }
        auto drawBox = [&] (juce::Rectangle<float> r, const juce::String& top, const juce::String& name, bool io, bool on)
        {
            g.setColour (juce::Colour (0xff111821));
            g.fillRect (r);
            g.setColour (io ? cBlue : (on ? cGrey : cGrid));
            g.drawRect (r, 2.0f);
            g.setColour (io ? cBeige : cDim);
            g.setFont (theme::mono (15.0f));
            g.drawText (top, r.reduced (12, 10).withHeight (20), juce::Justification::centredLeft);
            g.setColour (on ? cText : cDim);
            g.setFont (theme::sansSemi (20.0f));
            g.drawFittedText (name, r.reduced (12, 10).withTrimmedTop (26).toNearestInt(), juce::Justification::topLeft, 2);
        };
        drawBox (boxOf (0), "\"INPUT\"", "Bridge input", true, true);
        drawBox (boxOf (1), "\"OUTPUT\"", "Bridge output (sum)", true, true);
        for (auto& b : blocks)
            drawBox (boxOf (b.id), stageLabel (b) + "  " + typeName (b.type).toUpperCase(),
                     b.name + (b.enabled ? "" : " (bypassed)") + "\nmix " + juce::String (juce::roundToInt (b.mix * 100)) + "%", false, b.enabled && b.active);
        return img;
    }

    juce::Image drawVectorscope (const measure::Result& m)
    {
        juce::Image img (juce::Image::RGB, 1600, 900, true);
        juce::Graphics g (img);
        g.fillAll (cBg);
        g.setColour (cText);
        g.setFont (theme::sansSemi (34.0f));
        g.drawText ("Stereo analysis", 70, 28, 1400, 40, juce::Justification::centredLeft);
        g.setColour (cDim);
        g.setFont (theme::sans (20.0f));
        g.drawText ("Vectorscope of the output for decorrelated pink noise (-18 dBFS) - measured", 70, 70, 1400, 26, juce::Justification::centredLeft);
        const juce::Point<float> c (500.0f, 500.0f);
        const float R = 330.0f;
        g.setColour (cGrid);
        g.drawEllipse (c.x - R, c.y - R, 2 * R, 2 * R, 1.5f);
        g.drawLine (c.x, c.y - R, c.x, c.y + R, 1.0f);
        g.drawLine (c.x - R, c.y, c.x + R, c.y, 1.0f);
        g.setColour (cDim);
        g.setFont (theme::mono (17.0f));
        g.drawText ("M", (int) c.x - 10, (int) (c.y - R - 30), 20, 22, juce::Justification::centred);
        g.drawText ("L", (int) (c.x - R * 0.72f - 30), (int) (c.y - R * 0.72f - 20), 20, 22, juce::Justification::centred);
        g.drawText ("R", (int) (c.x + R * 0.72f + 10), (int) (c.y - R * 0.72f - 20), 20, 22, juce::Justification::centred);
        float peak = 1e-6f;
        for (size_t i = 0; i < m.scopeL.size(); ++i) peak = juce::jmax (peak, std::abs (m.scopeL[i]), std::abs (m.scopeR[i]));
        g.setColour (cBlue.withAlpha (0.35f));
        for (size_t i = 0; i < m.scopeL.size(); ++i)
        {
            const float l = m.scopeL[i] / peak, r = m.scopeR[i] / peak;
            const float x = (r - l) * 0.707f, y = (l + r) * 0.707f;
            g.fillRect (c.x + x * R * 0.7f, c.y - y * R * 0.7f, 2.0f, 2.0f);
        }
        g.setColour (cText);
        g.setFont (theme::sans (24.0f));
        int y = 260;
        auto line = [&] (const juce::String& a, const juce::String& b)
        {
            g.setColour (cDim); g.drawText (a, 960, y, 360, 30, juce::Justification::centredLeft);
            g.setColour (cText); g.drawText (b, 1320, y, 240, 30, juce::Justification::centredRight);
            y += 44;
        };
        line ("Correlation (mono input)", juce::String (m.corrMono, 3));
        line ("Correlation (wide input)", juce::String (m.corrWide, 3));
        line ("Side / mid energy (wide input)", juce::String (m.sideToMidDb, 1) + " dB");
        line ("Crosstalk L -> R", m.crosstalkDb < -150 ? juce::String ("< -150 dB") : juce::String (m.crosstalkDb, 1) + " dB");
        return img;
    }
}

// ================================================================== export

juce::File exportPackage (const juce::ValueTree& bridge, const Options& opt, const juce::File& userAudio,
                          std::function<void (double, const juce::String&)> progress, std::function<bool()> stop, juce::String& error)
{
    auto report = [&] (double p, const juce::String& s) { if (progress) progress (p, s); };
    const double fs = opt.sampleRate;
    const auto name = safeName (opt.projectName);

    // ---- folder + revision ----
    auto root = opt.baseFolder.getChildFile ("Atelier_Export");
    int revision = 1;
    auto dir = root.getChildFile (name);
    while (dir.exists())
        dir = root.getChildFile (name + "_rev" + juce::String (++revision).paddedLeft ('0', 2));
    for (auto sub : { "", "audio", "graphs", "measurements", "implementation" })
        if (! dir.getChildFile (sub).createDirectory())
        {
            error = "Could not create " + dir.getChildFile (sub).getFullPathName();
            return {};
        }
    juce::StringArray files;
    auto rel = [&] (const juce::File& f) { files.add (f.getRelativePathFrom (dir).replaceCharacter ('\\', '/')); return f; };

    // ---- introspection ----
    report (0.02, "Reading the bridge...");
    std::vector<int> order;
    std::vector<sonic::Graph::Edge> edges;
    auto blocks = describe (bridge, fs, order, edges);

    // ---- measurements ----
    report (0.05, "Measuring the bridge (frequency, harmonics, dynamics, stereo)...");
    auto m = measure::run (bridge, fs, [&] (double p) { report (0.05 + 0.35 * p, "Measuring the bridge..."); }, stop);
    if (stop && stop()) { error = "Cancelled."; return {}; }
    for (size_t i = 0; i < blocks.size(); ++i)
    {
        report (0.40 + 0.15 * (double) i / juce::jmax<size_t> (1, blocks.size()), "Measuring block " + blocks[i].name + "...");
        if (blocks[i].active) measureBlock (blocks[i], bridge, fs);
        if (stop && stop()) { error = "Cancelled."; return {}; }
    }

    // ---- audio ----
    bool deterministic = true;
    juce::StringArray nondetReasons;
    for (auto& b : blocks)
    {
        if (! b.active || ! b.enabled) continue;
        auto pv = [&] (const char* id) { for (auto& p : b.params) if (p.id == id) return p.value; return 0.0; };
        if (b.type == "character.tape" && (pv ("wow") > 0 || pv ("flutter") > 0 || pv ("noise_db") > -119)) { deterministic = false; nondetReasons.add (b.name + ": wow/flutter/hiss"); }
        if (b.type == "character.console" && pv ("noise_db") > -119) { deterministic = false; nondetReasons.add (b.name + ": noise"); }
        if (b.type == "mod.chorus" || b.type == "mod.flanger" || b.type == "mod.phaser") { deterministic = false; nondetReasons.add (b.name + ": LFO phase"); }
        if (b.type == "space.delay" && pv ("wow") > 0) { deterministic = false; nondetReasons.add (b.name + ": wow"); }
        if (b.type == "space.reverb" && pv ("mod_depth") > 0) { deterministic = false; nondetReasons.add (b.name + ": tail modulation"); }
    }

    std::vector<float> inL, inR, outL, outR;
    if (opt.includeAudio)
    {
        report (0.56, "Rendering audio references...");
        measure::referenceProgram (fs, inL, inR);
        measure::render (bridge, fs, inL, inR, outL, outR);
        writeWav (rel (dir.getChildFile ("audio/input_reference.wav")), fs, inL, inR);
        writeWav (rel (dir.getChildFile ("audio/processed_reference.wav")), fs, outL, outR);

        // comparison: alternating 2 s segments, dry (loudness matched) then processed
        double pw = 0, pd = 0;
        for (size_t i = 0; i < outL.size(); ++i) { pw += (double) outL[i] * outL[i]; pd += (double) inL[i] * inL[i]; }
        const float match = pd > 1e-12 ? (float) std::sqrt (pw / pd) : 1.0f;
        std::vector<float> cL (inL.size()), cR (inL.size());
        const size_t seg = (size_t) (2 * fs);
        for (size_t i = 0; i < inL.size(); ++i)
        {
            const bool wet = (i / seg) % 2 == 1;
            cL[i] = wet ? outL[i] : inL[i] * match;
            cR[i] = wet ? outR[i] : inR[i] * match;
        }
        writeWav (rel (dir.getChildFile ("audio/comparison.wav")), fs, cL, cR);

        std::vector<const BlockInfo*> inOrder;
        for (auto& b : blocks) if (b.active) inOrder.push_back (&b);
        std::sort (inOrder.begin(), inOrder.end(), [] (auto* a, auto* c) { return a->stage < c->stage; });
        for (auto* bp : inOrder)
        {
            const auto& b = *bp;
            std::vector<float> sL, sR;
            measure::render (measure::isolateBlock (bridge, b.id), fs, inL, inR, sL, sR);
            writeWav (rel (dir.getChildFile ("audio/stage_" + juce::String (b.stage).paddedLeft ('0', 2) + "_" + safeName (b.name) + ".wav")), fs, sL, sR);
            if (stop && stop()) { error = "Cancelled."; return {}; }
        }
        if (userAudio.existsAsFile())
        {
            juce::AudioFormatManager fm;
            fm.registerBasicFormats();
            if (std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (userAudio)); rd != nullptr)
            {
                const int len = (int) juce::jmin<juce::int64> (rd->lengthInSamples, (juce::int64) (rd->sampleRate * 30.0));
                juce::AudioBuffer<float> buf (2, len);
                rd->read (&buf, 0, len, 0, true, true);
                // resample to fs if needed (linear interpolation is adequate for a listening reference)
                const double ratio = rd->sampleRate / fs;
                const int n = (int) (len / ratio);
                std::vector<float> uL ((size_t) n), uR ((size_t) n), pL, pR;
                for (int i = 0; i < n; ++i)
                {
                    const double pos = i * ratio;
                    const int k = (int) pos;
                    const float f = (float) (pos - k);
                    const int k1 = juce::jmin (k + 1, len - 1);
                    uL[(size_t) i] = buf.getSample (0, k) * (1 - f) + buf.getSample (0, k1) * f;
                    uR[(size_t) i] = buf.getSample (rd->numChannels > 1 ? 1 : 0, k) * (1 - f) + buf.getSample (rd->numChannels > 1 ? 1 : 0, k1) * f;
                }
                measure::render (bridge, fs, uL, uR, pL, pR);
                writeWav (rel (dir.getChildFile ("audio/user_reference_original.wav")), fs, uL, uR);
                writeWav (rel (dir.getChildFile ("audio/user_reference_processed.wav")), fs, pL, pR);
            }
        }
    }

    // ---- graphs ----
    report (0.72, "Drawing graphs...");
    const juce::String cond = "fs " + juce::String (fs / 1000.0, 1) + " kHz  -  offline render of the saved bridge  -  Atelier " JucePlugin_VersionString;
    std::vector<std::pair<juce::String, juce::Image>> images;
    {
        PlotSpec p;
        p.title = "Frequency response (bridge)";
        p.subtitle = "Linear H1 estimate, white noise -18 dBFS RMS, 1/12-octave smoothing  -  ESTIMATED (linear part only)";
        p.xLabel = "Frequency (Hz)"; p.yLabel = "Gain (dB)";
        double mx = 1.0;
        for (double v : m.frMag) mx = juce::jmax (mx, std::abs (v));
        p.y0 = -std::ceil (mx * 1.3); p.y1 = std::ceil (mx * 1.3);
        p.series.push_back ({ "Magnitude", cBlue, m.frF, m.frMag });
        std::vector<double> cohScaled;
        for (double c : m.frCoh) cohScaled.push_back (p.y0 + c * (p.y1 - p.y0) * 0.15);
        p.series.push_back ({ "Coherence (0..1, bottom 15 %)", cGreen, m.frF, cohScaled, true });
        p.notes.add (cond + "  -  mean coherence " + juce::String (m.meanCoherence, 3));
        images.push_back ({ "frequency_response", renderPlot (p) });
    }
    {
        PlotSpec p;
        p.title = "Phase response (bridge)";
        p.subtitle = "Phase of the H1 estimate with the reported latency (" + juce::String (m.latency) + " samples) removed  -  ESTIMATED";
        p.xLabel = "Frequency (Hz)"; p.yLabel = "Phase (degrees)";
        p.y0 = -180; p.y1 = 180;
        p.series.push_back ({ "Phase", cBlue, m.frF, m.frPhaseDeg });
        p.notes.add (cond);
        images.push_back ({ "phase_response", renderPlot (p) });
    }
    {
        PlotSpec p;
        p.title = "Harmonic spectrum (bridge)";
        p.subtitle = "Output spectrum for a 1 kHz sine at -12 dBFS, FFT 16384 Blackman-Harris  -  MEASURED";
        p.xLabel = "Frequency (Hz)"; p.yLabel = "Level (dBFS)";
        p.y0 = -160; p.y1 = 0;
        Series s { "Output spectrum", cBlue, {}, {} };
        for (size_t k = 1; k < m.spectrumDb.size(); ++k) { s.x.push_back (k * m.spectrumBinHz); s.y.push_back (m.spectrumDb[k]); }
        p.series.push_back (s);
        p.notes.add (cond);
        images.push_back ({ "harmonic_spectrum", renderPlot (p) });
    }
    {
        PlotSpec p;
        p.title = "Harmonics vs input level (bridge)";
        p.subtitle = "1 kHz sine. THD and individual harmonics relative to the fundamental  -  MEASURED";
        p.logX = false; p.x0 = -36; p.x1 = 0; p.y0 = -140; p.y1 = 0;
        p.xLabel = "Input level (dBFS)"; p.yLabel = "dB / dBc";
        Series thd { "THD", cBeige, {}, {} }, h2 { "H2", cBlue, {}, {} }, h3 { "H3", cGrey, {}, {} };
        for (auto& l : m.levels)
        {
            thd.x.push_back (l.inDb); thd.y.push_back (l.thdDb);
            h2.x.push_back (l.inDb); h2.y.push_back (l.hDbc.size() > 0 ? l.hDbc[0] : -200);
            h3.x.push_back (l.inDb); h3.y.push_back (l.hDbc.size() > 1 ? l.hDbc[1] : -200);
        }
        p.series = { thd, h2, h3 };
        p.notes.add (cond);
        images.push_back ({ "thd_vs_level", renderPlot (p) });
    }
    {
        PlotSpec p;
        p.title = "Compression / level curve";
        p.subtitle = "Measured bridge output level vs input level (1 kHz, steady state), and static curves of compressor blocks";
        p.logX = false; p.x0 = -60; p.x1 = 0; p.y0 = -60; p.y1 = 12;
        p.xLabel = "Input level (dBFS)"; p.yLabel = "Output level (dBFS)";
        Series meas { "Bridge (MEASURED)", cBeige, {}, {} };
        for (auto& l : m.levels) { meas.x.push_back (l.inDb); meas.y.push_back (l.outDb); }
        p.series.push_back ({ "Unity", cGrid.brighter (0.5f), { -60.0, 0.0 }, { -60.0, 0.0 }, true });
        p.series.push_back (meas);
        for (auto& b : blocks)
            if (b.staticKind == "output_db_vs_input_db")
                p.series.push_back ({ b.name + " static (from DSP code)", cBlue, b.staticX, b.staticY, true });
        p.notes.add (cond);
        images.push_back ({ "compression_curve", renderPlot (p) });
    }
    {
        PlotSpec p;
        p.title = "Transient / dynamic response";
        p.subtitle = "1 kHz sine stepping -30 -> -6 -> -30 dBFS, 5 ms RMS frames  -  MEASURED";
        p.logX = false; p.x0 = 0; p.x1 = (double) m.envIn.size() * 5.0; p.y0 = -60; p.y1 = 6;
        p.xLabel = "Time (ms)"; p.yLabel = "Level (dBFS)";
        Series a { "Input", cGrey, {}, {} }, b { "Output", cBlue, {}, {} };
        for (size_t i = 0; i < m.envIn.size(); ++i) { a.x.push_back (i * 5.0); a.y.push_back (m.envIn[i]); b.x.push_back (i * 5.0); b.y.push_back (m.envOut[i]); }
        p.series = { a, b };
        p.notes.add ("Attack (63 %) " + timingText (m.attackMs, m.timingResolutionMs)
                     + "   Release 63 % / 90 % " + timingText (m.releaseMs, m.timingResolutionMs) + " / " + timingText (m.release90Ms, m.timingResolutionMs)
                     + "   Gain change at step " + juce::String (m.grStepDb, 1) + " dB");
        images.push_back ({ "transient_response", renderPlot (p) });
    }
    {
        PlotSpec p;
        p.title = "Impulse response (bridge)";
        p.subtitle = "Impulse of 0.01 (scaled x100), latency removed, first 20 ms  -  MEASURED";
        p.logX = false; p.x0 = 0; p.x1 = 20; p.y0 = -1; p.y1 = 1;
        p.xLabel = "Time (ms)"; p.yLabel = "Amplitude";
        Series s { "Impulse response", cBlue, {}, {} };
        for (size_t i = 0; i < m.ir.size() && i < (size_t) (0.02 * fs); ++i) { s.x.push_back (i / fs * 1000.0); s.y.push_back (m.ir[i]); }
        p.series = { s };
        images.push_back ({ "impulse_response", renderPlot (p) });
    }
    {
        PlotSpec p;
        p.title = "Nonlinear transfer curves";
        p.subtitle = "Static curves of the nonlinear blocks, computed from the same DSP code that processes the audio";
        p.logX = false; p.x0 = -1; p.x1 = 1; p.y0 = -1.5; p.y1 = 1.5;
        p.xLabel = "Input (full scale)"; p.yLabel = "Output";
        p.series.push_back ({ "Linear reference", cGrid.brighter (0.5f), { -1.0, 1.0 }, { -1.0, 1.0 }, true });
        const juce::Colour cols[] = { cBlue, cBeige, cGreen, cGrey };
        int ci = 0;
        for (auto& b : blocks)
            if (b.staticKind == "output_vs_input_fullscale")
                p.series.push_back ({ b.name, cols[ci++ % 4], b.staticX, b.staticY });
        images.push_back ({ "transfer_curves", renderPlot (p) });
    }
    {
        PlotSpec p;
        p.title = "EQ curves";
        p.subtitle = "Static magnitude of each EQ block (from the filter coefficients)";
        p.xLabel = "Frequency (Hz)"; p.yLabel = "Gain (dB)";
        double mx = 3.0;
        for (auto& b : blocks) if (b.staticKind == "magnitude_db_vs_hz") for (double v : b.staticY) mx = juce::jmax (mx, std::abs (v));
        p.y0 = -std::ceil (mx * 1.2); p.y1 = std::ceil (mx * 1.2);
        const juce::Colour cols[] = { cBeige, cBlue, cGreen, cGrey };
        int ci = 0;
        for (auto& b : blocks)
            if (b.staticKind == "magnitude_db_vs_hz")
                p.series.push_back ({ b.name, cols[ci++ % 4], b.staticX, b.staticY });
        if (ci > 0) images.push_back ({ "eq_curves", renderPlot (p) });
    }
    images.push_back ({ "stereo_analysis", drawVectorscope (m) });
    images.push_back ({ "signal_chain", drawChain (blocks, edges, opt.projectName) });
    for (auto& [n, img] : images)
        savePng (img, rel (dir.getChildFile ("graphs/" + n + ".png")));

    // ---- CSV ----
    report (0.80, "Writing measurement data...");
    {
        juce::String csv = "measurement,condition,x,x_unit,y,y_unit,classification\n";
        for (size_t i = 0; i < m.frF.size(); ++i)
        {
            csv << "frequency_response_mag,white noise -18 dBFS RMS," << fmtNum (m.frF[i], 1) << ",Hz," << fmtNum (m.frMag[i], 3) << ",dB,estimated\n";
            csv << "frequency_response_phase,white noise -18 dBFS RMS," << fmtNum (m.frF[i], 1) << ",Hz," << fmtNum (m.frPhaseDeg[i], 2) << ",deg,estimated\n";
            csv << "coherence,white noise -18 dBFS RMS," << fmtNum (m.frF[i], 1) << ",Hz," << fmtNum (m.frCoh[i], 4) << ",ratio,measured\n";
        }
        for (auto& l : m.levels)
        {
            csv << "output_level,1 kHz sine," << fmtNum (l.inDb, 1) << ",dBFS," << fmtNum (l.outDb, 3) << ",dBFS,measured\n";
            csv << "thd,1 kHz sine," << fmtNum (l.inDb, 1) << ",dBFS," << fmtNum (l.thdDb, 2) << ",dB,measured\n";
            for (size_t k = 0; k < l.hDbc.size(); ++k)
                csv << "h" << (k + 2) << ",1 kHz sine," << fmtNum (l.inDb, 1) << ",dBFS," << fmtNum (l.hDbc[k], 2) << ",dBc,measured\n";
        }
        for (size_t i = 0; i < m.thdFreqF.size(); ++i)
            csv << "thd_vs_frequency,sine -12 dBFS," << fmtNum (m.thdFreqF[i], 0) << ",Hz," << fmtNum (m.thdFreqDb[i], 2) << ",dB,measured\n";
        for (size_t i = 0; i < m.envIn.size(); ++i)
            csv << "step_envelope_out,1 kHz -30/-6/-30 dBFS," << (i * 5) << ",ms," << fmtNum (m.envOut[i], 2) << ",dBFS,measured\n";
        csv << "attack_time_63pct,step -30 to -6 dBFS,0,,"  << fmtNum (m.attackMs, 2) << ",ms,measured\n";
        csv << "release_time_63pct,step -6 to -30 dBFS,0,," << fmtNum (m.releaseMs, 2) << ",ms,measured\n";
        csv << "release_time_90pct,step -6 to -30 dBFS,0,," << fmtNum (m.release90Ms, 2) << ",ms,measured\n";
        csv << "timing_resolution,synchronous demodulation,0,," << fmtNum (m.timingResolutionMs, 2) << ",ms,measured\n";
        csv << "crosstalk_l_to_r,pink noise -18 dBFS left only,0,," << fmtNum (m.crosstalkDb, 2) << ",dB,measured\n";
        csv << "correlation_mono_input,pink noise -18 dBFS L=R,0,," << fmtNum (m.corrMono, 4) << ",ratio,measured\n";
        csv << "correlation_wide_input,decorrelated pink noise,0,," << fmtNum (m.corrWide, 4) << ",ratio,measured\n";
        csv << "noise_floor,silence,0,," << (m.noiseFloorDb <= measure::Result::kNoiseFloorLimit ? juce::String ("below_-150") : fmtNum (m.noiseFloorDb, 1)) << ",dBFS,measured\n";
        csv << "imd_smpte,60 Hz + 7 kHz 4:1 at -12 dBFS,0,," << fmtNum (m.imdPct, 4) << ",percent,measured\n";
        csv << "latency,reported by the bridge,0,," << m.latency << ",samples,measured\n";
        writeText (rel (dir.getChildFile ("measurements/measurement_data.csv")), csv);

        juce::String cond2 = "parameter,value\nsample_rate_hz," + juce::String (fs) + "\nrender,offline from saved bridge state\nblock_size,512\n"
                             "fft_harmonics,16384 Blackman-Harris 4-term\nfft_transfer,16384 Hann (Welch H1 50% overlap)\nlevel_reference,0 dBFS = full-scale sine peak\n"
                             "atelier_version," JucePlugin_VersionString "\n";
        writeText (rel (dir.getChildFile ("measurements/measurement_conditions.csv")), cond2);
    }

    // ---- JSON ----
    report (0.84, "Writing blueprint.json...");
    const juce::String uuid = juce::Uuid().toDashedString();
    const juce::String created = juce::Time::getCurrentTime().toISO8601 (true);
    juce::var doc = obj();
    set (doc, "$schema", "schema.json");
    set (doc, "schema", kSchemaId);
    set (doc, "blueprint_id", uuid);
    set (doc, "revision", revision);
    set (doc, "created_utc", created);
    {
        juce::var gen = obj();
        set (gen, "name", "Atelier by R4N");
        set (gen, "version", JucePlugin_VersionString);
        set (doc, "generator", gen);
    }
    {
        juce::var pj = obj();
        set (pj, "name", opt.projectName);
        set (pj, "objective", opt.objective);
        set (doc, "project", pj);
    }
    {
        juce::var c = obj();
        set (c, "sample_rate_hz", fs);
        set (c, "channels", 2);
        set (c, "channel_layout", "stereo (mono hosts: left channel, right mirrored)");
        set (c, "internal_precision", "64-bit double for filters/state, 32-bit float audio buffers");
        set (c, "level_reference", "0 dBFS = peak of a full-scale sine (amplitude 1.0)");
        set (doc, "conditions", c);
    }
    {
        juce::var gr = obj();
        set (gr, "input_node_id", 0);
        set (gr, "output_node_id", 1);
        set (gr, "summing_rule", "A block's input is the sum of all wires into it; Output is the sum of all wires into it. Every path into a sum is delayed so all arrive aligned (latency compensation).");
        set (gr, "mix_rule", "out = (dry_delayed x (1 - mix) + processed x mix) x 10^(output_gain_db / 20); dry_delayed = block input delayed by the block latency. Disabled block = exact pass-through with zero latency.");
        juce::Array<juce::var> ord;
        for (int id : order) ord.add (id);
        set (gr, "processing_order", ord);
        juce::Array<juce::var> wires;
        for (auto& e : edges) { juce::var w = obj(); set (w, "from", e.from); set (w, "to", e.to); wires.add (w); }
        set (gr, "edges", wires);
        juce::Array<juce::var> nodes;
        for (auto& b : blocks)
        {
            juce::var n = obj();
            set (n, "id", b.id);
            set (n, "type", juce::String (b.type));
            set (n, "type_name", typeName (b.type));
            set (n, "name", b.name);
            set (n, "enabled", b.enabled);
            set (n, "in_signal_path", b.active);
            set (n, "stage", b.stage);
            set (n, "mix", b.mix);
            set (n, "output_gain_db", b.outDb);
            set (n, "latency_samples", b.latency);
            set (n, "topology", b.topology);
            juce::Array<juce::var> ps;
            for (auto& p : b.params)
            {
                juce::var pv = obj();
                set (pv, "id", p.id);
                set (pv, "name", p.name);
                set (pv, "value", p.value);
                set (pv, "value_text", p.valueText);
                set (pv, "unit", p.unit);
                set (pv, "min", p.min);
                set (pv, "max", p.max);
                set (pv, "default", p.def);
                set (pv, "group", p.group);
                set (pv, "description", p.desc);
                set (pv, "active", p.active);
                if (! p.active) set (pv, "inactive_reason", p.inactiveReason);
                if (! p.choices.isEmpty()) { juce::Array<juce::var> ch; for (auto& c : p.choices) ch.add (c); set (pv, "choices", ch); }
                ps.add (pv);
            }
            set (n, "parameters", ps);
            if (b.staticKind.isNotEmpty())
            {
                juce::var sc = obj();
                set (sc, "kind", b.staticKind);
                set (sc, "classification", "exact (computed from the implemented DSP)");
                juce::Array<juce::var> xs, ys;
                for (size_t i = 0; i < b.staticX.size(); i += (b.staticX.size() > 100 ? 2 : 1)) { xs.add (b.staticX[i]); ys.add (b.staticY[i]); }
                set (sc, "x", xs); set (sc, "y", ys);
                set (n, "static_characteristic", sc);
            }
            if (b.active)
            {
                juce::var im = obj();
                set (im, "classification", "measured (block in isolation, offline)");
                set (im, "gain_db_1k_m18", b.gain18);
                set (im, "thd_db_1k_m18", b.thd18);
                set (im, "thd_db_1k_m6", b.thd6);
                set (im, "h2_dbc_1k_m18", b.h2_18);
                set (im, "h3_dbc_1k_m18", b.h3_18);
                juce::Array<juce::var> fr;
                for (size_t i = 0; i < b.frF.size(); ++i) { juce::var pt = obj(); set (pt, "hz", b.frF[i]); set (pt, "db", b.frDb[i]); fr.add (pt); }
                set (im, "frequency_response", fr);
                set (n, "isolated_measurements", im);
            }
            nodes.add (n);
        }
        set (gr, "nodes", nodes);
        set (gr, "total_latency_samples", m.latency);
        set (doc, "signal_graph", gr);
    }
    {
        juce::var ms = obj();
        juce::var fr = obj();
        set (fr, "classification", "estimated (linear H1 transfer function; nonlinear and time-variant parts lower the coherence)");
        set (fr, "conditions", "white noise -18 dBFS RMS, 8 s, FFT 16384 Hann, 1/12-octave smoothing");
        set (fr, "mean_coherence", m.meanCoherence);
        juce::Array<juce::var> pts;
        for (size_t i = 0; i < m.frF.size(); i += 2) { juce::var pt = obj(); set (pt, "hz", m.frF[i]); set (pt, "db", m.frMag[i]); set (pt, "phase_deg", m.frPhaseDeg[i]); set (pt, "coherence", m.frCoh[i]); pts.add (pt); }
        set (fr, "points", pts);
        set (ms, "frequency_response", fr);
        juce::Array<juce::var> lv;
        for (auto& l : m.levels)
        {
            juce::var pt = obj();
            set (pt, "input_dbfs", l.inDb); set (pt, "output_dbfs", l.outDb); set (pt, "thd_percent", l.thdPct); set (pt, "thd_db", l.thdDb);
            juce::Array<juce::var> h; for (double v : l.hDbc) h.add (v); set (pt, "h2_to_h10_dbc", h);
            lv.add (pt);
        }
        juce::var hv = obj();
        set (hv, "classification", "measured");
        set (hv, "conditions", "1 kHz sine, 1.5 s, last 16384 samples, Blackman-Harris");
        set (hv, "points", lv);
        set (ms, "harmonics_vs_level", hv);
        juce::Array<juce::var> tf;
        for (size_t i = 0; i < m.thdFreqF.size(); ++i) { juce::var pt = obj(); set (pt, "hz", m.thdFreqF[i]); set (pt, "thd_db", m.thdFreqDb[i]); tf.add (pt); }
        set (ms, "thd_vs_frequency_m12dbfs", tf);
        juce::var dyn = obj();
        set (dyn, "classification", "measured");
        set (dyn, "conditions", "1 kHz sine: 1 s at -30, 1 s at -6, 3 s at -30 dBFS; gain from synchronous 1 kHz demodulation (2 ms Hann window, 0.1 ms hop); 63 % and 90 % criteria");
        set (dyn, "timing_resolution_ms", m.timingResolutionMs);
        auto timeVar = [&] (double v) { return v < 0 ? juce::var() : juce::var (v); };
        set (dyn, "attack_ms", timeVar (m.attackMs));
        set (dyn, "attack_below_resolution", m.attackMs >= 0 && m.attackMs < m.timingResolutionMs);
        set (dyn, "release_ms", timeVar (m.releaseMs));
        set (dyn, "release_90pct_ms", timeVar (m.release90Ms));
        set (dyn, "note", "release_ms is the 63 % point; release_90pct_ms reveals slow second stages (e.g. optical / program-dependent release).");
        set (dyn, "gain_change_db", m.grStepDb);
        set (ms, "dynamics", dyn);
        juce::var st = obj();
        set (st, "classification", "measured");
        set (st, "crosstalk_l_to_r_db", m.crosstalkDb);
        set (st, "correlation_mono_input", m.corrMono);
        set (st, "correlation_decorrelated_input", m.corrWide);
        set (st, "side_to_mid_db_decorrelated_input", m.sideToMidDb);
        set (ms, "stereo", st);
        if (m.noiseFloorDb <= measure::Result::kNoiseFloorLimit)
        {
            set (ms, "noise_floor_dbfs", juce::var());
            set (ms, "noise_floor_note", "below the -150 dBFS measurement floor (digitally silent)");
        }
        else
            set (ms, "noise_floor_dbfs", m.noiseFloorDb);
        set (ms, "imd_smpte_percent", m.imdPct);
        set (ms, "latency_samples", m.latency);
        set (doc, "measurements", ms);
    }
    juce::Array<juce::var> targets;
    auto addTarget = [&] (const juce::String& id, const juce::String& metric, double target, double tol, const juce::String& unit, const juce::String& method)
    {
        juce::var t = obj();
        set (t, "id", id); set (t, "metric", metric); set (t, "target", target); set (t, "tolerance", tol); set (t, "unit", unit); set (t, "method", method);
        targets.add (t);
    };
    for (double f : { 31.5, 63.0, 125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0, 16000.0 })
    {
        size_t best = 0;
        for (size_t i = 0; i < m.frF.size(); ++i) if (std::abs (std::log (m.frF[i] / f)) < std::abs (std::log (m.frF[best] / f))) best = i;
        if (m.frF.empty()) break;
        const bool reliable = m.frCoh[best] > 0.9;
        addTarget ("FR_" + juce::String ((int) f), "frequency response at " + juce::String (f) + " Hz", m.frMag[best], reliable ? 0.5 : 1.5, "dB",
                   "white noise -18 dBFS RMS, H1 estimate" + juce::String (reliable ? "" : " (low coherence: wider tolerance)"));
    }
    for (auto& l : m.levels)
    {
        addTarget ("LVL_" + juce::String ((int) l.inDb), "output level, 1 kHz at " + juce::String (l.inDb) + " dBFS", l.outDb, 0.5, "dBFS", "steady-state sine");
        addTarget ("THD_" + juce::String ((int) l.inDb), "THD, 1 kHz at " + juce::String (l.inDb) + " dBFS", l.thdDb, l.thdDb < -80 ? 6.0 : 2.0, "dB", "Blackman-Harris FFT 16384");
        if (! l.hDbc.empty() && l.hDbc[0] > -90)
            addTarget ("H2_" + juce::String ((int) l.inDb), "H2, 1 kHz at " + juce::String (l.inDb) + " dBFS", l.hDbc[0], 2.0, "dBc", "harmonic analysis");
        if (l.hDbc.size() > 1 && l.hDbc[1] > -90)
            addTarget ("H3_" + juce::String ((int) l.inDb), "H3, 1 kHz at " + juce::String (l.inDb) + " dBFS", l.hDbc[1], 2.0, "dBc", "harmonic analysis");
    }
    if (m.attackMs >= 0) addTarget ("ATTACK", "attack time (63 %)", m.attackMs, juce::jmax (1.0, m.timingResolutionMs, m.attackMs * 0.25), "ms", "step test, synchronous demodulation");
    if (m.releaseMs >= 0) addTarget ("RELEASE", "release time (63 %)", m.releaseMs, juce::jmax (5.0, m.releaseMs * 0.25), "ms", "step test, synchronous demodulation");
    if (m.release90Ms >= 0) addTarget ("RELEASE90", "release time (90 %)", m.release90Ms, juce::jmax (10.0, m.release90Ms * 0.25), "ms", "step test, synchronous demodulation");
    addTarget ("XTALK", "crosstalk L->R", m.crosstalkDb, 3.0, "dB", "left-only pink noise");
    addTarget ("CORR_WIDE", "output correlation, decorrelated input", m.corrWide, 0.05, "ratio", "decorrelated pink noise");
    if (deterministic && opt.includeAudio)
        addTarget ("NULL", "residual: plugin(input_reference.wav) - processed_reference.wav", -40.0, 0.0, "dB re. processed RMS (maximum)",
                   "render input_reference.wav at the same sample rate, align latency, subtract");
    set (doc, "validation_targets", targets);
    {
        juce::Array<juce::var> notes;
        notes.add ("REQUIREMENT LEVELS: 'MUST' = the signal graph, block order, wiring, parameter values, mix rule, summing rule and latency alignment in this file. "
                   "'SHOULD' = match the measured targets within tolerance. 'MAY' = implementation choices described in each block's topology text.");
        notes.add ("Each block's 'topology' describes how Atelier implements it; reproducing it exactly is the most reliable way to pass validation.");
        notes.add ("Black-box style measurements (frequency response, coherence) describe behaviour; they are not a circuit description.");
        if (! deterministic)
            notes.add ("Not sample-deterministic (" + nondetReasons.joinIntoString ("; ") + "): compare spectra/statistics instead of nulling.");
        set (doc, "implementation_notes", notes);
    }
    // file list written last (below)

    // ---- schema ----
    const juce::String schema = R"SCHEMA({
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "$id": "atelier.blueprint/1.0",
  "title": "Atelier DSP blueprint",
  "type": "object",
  "required": ["schema", "blueprint_id", "revision", "created_utc", "generator", "project", "conditions", "signal_graph", "measurements", "validation_targets"],
  "properties": {
    "schema": { "const": "atelier.blueprint/1.0" },
    "blueprint_id": { "type": "string", "description": "UUID of this export" },
    "revision": { "type": "integer", "minimum": 1, "description": "Increments for each export of the same project name" },
    "created_utc": { "type": "string", "format": "date-time" },
    "generator": { "type": "object", "properties": { "name": { "type": "string" }, "version": { "type": "string" } } },
    "project": { "type": "object", "properties": { "name": { "type": "string" }, "objective": { "type": "string" } } },
    "conditions": { "type": "object", "properties": { "sample_rate_hz": { "type": "number" }, "channels": { "type": "integer" } } },
    "signal_graph": {
      "type": "object",
      "required": ["input_node_id", "output_node_id", "processing_order", "edges", "nodes"],
      "properties": {
        "input_node_id": { "const": 0 },
        "output_node_id": { "const": 1 },
        "summing_rule": { "type": "string" },
        "mix_rule": { "type": "string" },
        "processing_order": { "type": "array", "items": { "type": "integer" }, "description": "Topological order of the blocks in the signal path" },
        "edges": { "type": "array", "items": { "type": "object", "required": ["from", "to"], "properties": { "from": { "type": "integer" }, "to": { "type": "integer" } } } },
        "total_latency_samples": { "type": "integer" },
        "nodes": {
          "type": "array",
          "items": {
            "type": "object",
            "required": ["id", "type", "name", "enabled", "mix", "output_gain_db", "latency_samples", "parameters"],
            "properties": {
              "id": { "type": "integer", "minimum": 2 },
              "type": { "type": "string", "description": "Block type id, e.g. dynamics.compressor" },
              "name": { "type": "string" },
              "enabled": { "type": "boolean" },
              "in_signal_path": { "type": "boolean" },
              "stage": { "type": "integer", "minimum": 0, "description": "1-based position in processing order; 0 = not in the signal path" },
              "mix": { "type": "number", "minimum": 0, "maximum": 1 },
              "output_gain_db": { "type": "number" },
              "latency_samples": { "type": "integer" },
              "topology": { "type": "string" },
              "parameters": {
                "type": "array",
                "items": {
                  "type": "object",
                  "required": ["id", "name", "value", "unit", "min", "max", "default"],
                  "properties": {
                    "id": { "type": "string" }, "name": { "type": "string" }, "value": { "type": "number" }, "value_text": { "type": "string" },
                    "unit": { "type": "string", "description": "dB, dBc, Hz, ms, s, %, :1, deg, oct, x or empty (dimensionless)" },
                    "min": { "type": "number" }, "max": { "type": "number" }, "default": { "type": "number" },
                    "group": { "type": "string" }, "description": { "type": "string" },
                    "choices": { "type": "array", "items": { "type": "string" }, "description": "Present for discrete parameters: value is the index" }
                  }
                }
              },
              "static_characteristic": { "type": "object" },
              "isolated_measurements": { "type": "object" }
            }
          }
        }
      }
    },
    "measurements": { "type": "object" },
    "validation_targets": {
      "type": "array",
      "items": { "type": "object", "required": ["id", "metric", "target", "tolerance", "unit", "method"],
        "properties": { "id": { "type": "string" }, "metric": { "type": "string" }, "target": { "type": "number" }, "tolerance": { "type": "number" }, "unit": { "type": "string" }, "method": { "type": "string" } } }
    },
    "implementation_notes": { "type": "array", "items": { "type": "string" } },
    "files": { "type": "array", "items": { "type": "string" } }
  }
}
)SCHEMA";
    writeText (rel (dir.getChildFile ("schema.json")), schema);

    // ---- markdown documents ----
    report (0.88, "Writing documents...");
    juce::String chainText;
    for (int id : order)
        for (auto& b : blocks)
            if (b.id == id)
                chainText << "- **" << stageLabel (b) << " " << b.name << "** (`" << juce::String (b.type) << "`), mix "
                          << juce::roundToInt (b.mix * 100) << " %, output " << juce::String (b.outDb, 1) << " dB, latency " << b.latency
                          << " samples" << (b.enabled ? "" : ", **bypassed**") << "\n";
    juce::String wiring;
    auto nodeName = [&] (int id) -> juce::String
    {
        if (id == 0) return "Input";
        if (id == 1) return "Output";
        for (auto& b : blocks) if (b.id == id) return stageLabel (b) + " " + b.name + " (node #" + juce::String (id) + ")";
        return "#" + juce::String (id);
    };
    for (auto& e : edges) wiring << "- " << nodeName (e.from) << " -> " << nodeName (e.to) << "\n";

    juce::String readme;
    readme << "# " << opt.projectName << " - Atelier blueprint (revision " << revision << ")\n\n";
    if (opt.objective.isNotEmpty()) readme << "**Sonic objective:** " << opt.objective << "\n\n";
    readme << "Blueprint id `" << uuid << "`, created " << created << " with Atelier " JucePlugin_VersionString " at "
           << juce::String (fs / 1000.0, 1) << " kHz.\n\n"
           << "## What this is\n\nAn engineering specification of a sound designed in Atelier: the exact signal graph and every parameter value, "
              "measurements of how it behaves, graphs, and audio references. It is written to be implemented as a stand-alone VST3 plugin "
              "and then validated against these references.\n\n"
           << "## How to use it (for the implementer)\n\n"
              "1. Read `blueprint.json` - the authoritative, machine-readable specification (schema: `schema.json`).\n"
              "2. Read `implementation/dsp_requirements.md` - the same content as prose, block by block.\n"
              "3. Implement the graph exactly: blocks, order, wires, mix rule, summing rule and latency alignment.\n"
              "4. Validate with `implementation/validation_tests.md` using `audio/input_reference.wav` -> compare with `audio/processed_reference.wav`.\n\n"
           << "## Requirement levels\n\n"
              "- **MUST** (exact): graph, wiring, parameter values and units, processing order, mix rule, summing rule, latency alignment.\n"
              "- **SHOULD** (measured): frequency response, level/compression curve, harmonics, timing, stereo values within the tolerances in `validation_targets`.\n"
              "- **MAY** (suggested): the internal topology described for each block. Reproducing it is the most reliable way to pass validation.\n\n"
           << "## Signal path (processing order)\n\n" << (chainText.isEmpty() ? "_(empty bridge: Input -> Output)_\n" : chainText)
           << "\n### Wires\n\n" << wiring
           << "\nTotal latency: **" << m.latency << " samples** (" << juce::String (m.latency / fs * 1000.0, 2) << " ms).\n\n"
           << "## Key measurements (offline, from the saved bridge)\n\n"
           << "| Measurement | Value | Classification |\n|---|---|---|\n";
    for (auto& l : m.levels)
        readme << "| 1 kHz at " << juce::String (l.inDb, 0) << " dBFS -> output | " << juce::String (l.outDb, 2) << " dBFS, THD " << juce::String (l.thdPct, 3) << " % | measured |\n";
    readme << "| Attack (63 %) | " << timingText (m.attackMs, m.timingResolutionMs) << " | measured |\n"
           << "| Release (63 % / 90 %) | " << timingText (m.releaseMs, m.timingResolutionMs) << " / " << timingText (m.release90Ms, m.timingResolutionMs) << " | measured |\n"
           << "| Mean coherence (linearity) | " << juce::String (m.meanCoherence, 3) << " | measured |\n"
           << "| Crosstalk L->R | " << (m.crosstalkDb < -150 ? juce::String ("< -150 dB") : juce::String (m.crosstalkDb, 1) + " dB") << " | measured |\n"
           << "| Output correlation (wide input) | " << juce::String (m.corrWide, 3) << " | measured |\n"
           << "| Noise floor | " << (m.noiseFloorDb <= measure::Result::kNoiseFloorLimit ? juce::String ("< -150 dBFS") : juce::String (m.noiseFloorDb, 1) + " dBFS") << " | measured |\n"
           << "| IMD (SMPTE) | " << juce::String (m.imdPct, 4) << " % | measured |\n\n"
           << "## Files\n\n";
    // file list placeholder replaced at the end
    readme << "%%FILES%%\n\n## Honesty notes\n\n"
              "- Frequency/phase response is a linear (H1) estimate; where coherence is low the system is nonlinear or time-variant there.\n"
              "- Block topologies describe Atelier's implementation, not any real circuit. No hardware or third-party plugin is claimed to be reproduced.\n"
           << (deterministic ? juce::String() : "- The chain is not sample-deterministic (" + nondetReasons.joinIntoString ("; ") + "); compare statistically.\n");

    juce::String req;
    req << "# DSP requirements - " << opt.projectName << " (rev " << revision << ")\n\n"
        << "## Global (MUST)\n\n"
        << "- Sample rate of the reference: " << fs << " Hz. Coefficients must be recomputed for other rates (all frequencies are given in Hz).\n"
        << "- Stereo in / stereo out. Mono hosts: process the left channel; mirror to right.\n"
        << "- Filters and state in 64-bit double; audio buffers may be 32-bit float.\n"
        << "- Graph semantics: a block's input = sum of all wires into it. Output = sum of all wires into Output. Every path into a sum is delayed so all paths arrive aligned (latency compensation).\n"
        << "- Mix rule per block: `out = (dry_delayed*(1-mix) + processed*mix) * 10^(gain_db/20)`; `dry_delayed` = block input delayed by that block's latency. A bypassed block passes its input unchanged with zero latency.\n"
        << "- Report total latency " << m.latency << " samples to the host.\n\n"
        << "## Processing order\n\n" << chainText << "\n### Wires\n\n" << wiring << "\n";
    for (int id : order)
        for (auto& b : blocks)
        {
            if (b.id != id) continue;
            req << "## Block " << stageLabel (b) << " - " << b.name << " (`" << juce::String (b.type) << "`)\n\n"
                << "- Enabled: " << (b.enabled ? "yes" : "no (bypassed)") << "; mix " << juce::String (b.mix, 3) << "; output gain " << juce::String (b.outDb, 2)
                << " dB; latency " << b.latency << " samples.\n\n"
                << "**Implemented topology (MAY - reproducing it is recommended):** " << b.topology << "\n\n"
                << "**Parameters (MUST):**\n\n| id | name | value | unit | range | default |\n|---|---|---|---|---|---|\n";
            const bool eq = b.type == "tone.eq";
            for (auto& p : b.params)
            {
                if (eq && p.group.startsWith ("Band"))
                {
                    // only list active bands
                    const auto band = p.group;
                    bool on = false;
                    for (auto& q : b.params) if (q.group == band && q.id.endsWith ("_on")) on = q.value > 0.5;
                    if (! on) continue;
                }
                req << "| `" << p.id << "` | " << p.name << " | " << (p.choices.isEmpty() ? juce::String (p.value, 4) : p.valueText + " (" + juce::String ((int) p.value) + ")")
                    << " | " << (p.unit.isEmpty() ? "-" : p.unit) << " | " << juce::String (p.min, 3) << " .. " << juce::String (p.max, 3) << " | " << juce::String (p.def, 3) << (p.active ? juce::String() : " (inactive: " + p.inactiveReason + ")") << " |\n";
            }
            if (eq) req << "\n_Bands that are off are omitted here; all bands appear in blueprint.json._\n";
            if (b.active)
                req << "\n**Measured in isolation (SHOULD):** gain at 1 kHz/-18 dBFS " << juce::String (b.gain18, 2) << " dB, THD " << juce::String (b.thd18, 1)
                    << " dB (-18 dBFS), " << juce::String (b.thd6, 1) << " dB (-6 dBFS); H2 " << juce::String (b.h2_18, 1) << " dBc, H3 " << juce::String (b.h3_18, 1) << " dBc.\n";
            req << "\n";
        }

    juce::String val;
    val << "# Validation tests - " << opt.projectName << " (rev " << revision << ")\n\n"
        << "All targets are in `blueprint.json` -> `validation_targets` (machine-readable). Render at " << fs << " Hz, stereo, block size 512 (any block size must give the same result).\n\n"
        << "## 1. Static configuration (MUST)\n\n- Every parameter value in `blueprint.json` is reproduced exactly.\n- Reported latency equals the implementation's actual latency (impulse test).\n\n"
        << "## 2. Measured behaviour (SHOULD)\n\n| id | metric | target | tolerance | unit | method |\n|---|---|---|---|---|---|\n";
    for (auto& t : targets)
    {
        auto* o = t.getDynamicObject();
        val << "| " << o->getProperty ("id").toString() << " | " << o->getProperty ("metric").toString() << " | " << juce::String ((double) o->getProperty ("target"), 2)
            << " | " << juce::String ((double) o->getProperty ("tolerance"), 2) << " | " << o->getProperty ("unit").toString() << " | " << o->getProperty ("method").toString() << " |\n";
    }
    val << "\n## 3. Listening / audio comparison\n\n"
        << "- Process `audio/input_reference.wav` and compare with `audio/processed_reference.wav` (latency removed in the reference).\n"
        << (deterministic ? "- The chain is deterministic: after latency alignment the residual should be below -40 dB relative to the processed RMS when implemented exactly.\n"
                          : "- The chain contains modulation/noise (" + nondetReasons.joinIntoString ("; ") + "): compare 1/3-octave spectra (+-1 dB) and loudness instead of nulling.\n")
        << "- `audio/stage_XX_*.wav` are each block rendered in isolation on the same input: use them to validate block by block.\n"
        << "- `audio/comparison.wav` alternates 2 s dry (loudness matched) and 2 s processed for listening.\n\n"
        << "## 4. Re-measure in Atelier\n\nLoad the new plugin before Atelier, route the unprocessed signal to Atelier's sidechain, select REFERENCE in the readout and compare against these graphs.\n";

    // ---- finish file list, JSON, docs ----
    rel (dir.getChildFile ("README.md"));
    rel (dir.getChildFile ("blueprint.json"));
    rel (dir.getChildFile ("implementation/dsp_requirements.md"));
    rel (dir.getChildFile ("implementation/validation_tests.md"));
    if (opt.includePdf) rel (dir.getChildFile ("blueprint.pdf"));
    files.sort (true);
    juce::Array<juce::var> fl;
    juce::String fileMd;
    for (auto& f : files) { fl.add (f); fileMd << "- `" << f << "`\n"; }
    set (doc, "files", fl);
    writeText (dir.getChildFile ("blueprint.json"), juce::JSON::toString (doc, false, 6));
    writeText (dir.getChildFile ("README.md"), readme.replace ("%%FILES%%", fileMd));
    writeText (dir.getChildFile ("implementation/dsp_requirements.md"), req);
    writeText (dir.getChildFile ("implementation/validation_tests.md"), val);

    // ---- PDF ----
    if (opt.includePdf)
    {
        report (0.93, "Writing blueprint.pdf...");
        PdfWriter pdf;
        pdf.newPage();
        pdf.rect (0, 0, PdfWriter::pageW, 120, juce::Colour (0xff0c1117));
        pdf.text (50, 60, "Atelier", 30, PdfWriter::helveticaBold, juce::Colour (0xffc9b28a));
        pdf.text (50, 90, "DSP blueprint - revision " + juce::String (revision), 12, PdfWriter::helvetica, juce::Colour (0xffc6d0dc));
        pdf.cursorY = 150;
        pdf.paragraph (opt.projectName, 20, PdfWriter::helveticaBold);
        if (opt.objective.isNotEmpty()) pdf.paragraph ("Sonic objective: " + opt.objective, 11);
        pdf.paragraph ("Blueprint " + uuid + "  -  " + created + "  -  " + juce::String (fs / 1000.0, 1) + " kHz  -  Atelier " JucePlugin_VersionString, 9, PdfWriter::courier);
        auto mdToPdf = [&] (const juce::String& md)
        {
            for (auto line : juce::StringArray::fromLines (md))
            {
                line = line.replace ("**", "").replace ("`", "");
                if (line.startsWith ("# ")) pdf.heading (line.substring (2), 16);
                else if (line.startsWith ("## ")) pdf.heading (line.substring (3), 13);
                else if (line.startsWith ("### ")) pdf.paragraph (line.substring (4), 11, PdfWriter::helveticaBold);
                else if (line.startsWith ("|---")) continue;
                else if (line.startsWith ("|")) pdf.paragraph (line.replace ("|", "  ").trim(), 8, PdfWriter::courier, juce::Colours::black, 0, 1);
                else if (line.trim().isEmpty()) pdf.cursorY += 4;
                else pdf.paragraph (line, 9.5f, PdfWriter::helvetica, juce::Colours::black, line.startsWith ("- ") ? 8.0f : 0.0f, 2);
            }
        };
        mdToPdf (readme.replace ("%%FILES%%", fileMd));
        pdf.newPage();
        pdf.heading ("Graphs", 16);
        for (auto& [n, img] : images)
        {
            pdf.image (img, PdfWriter::pageW - 2 * PdfWriter::margin);
            pdf.paragraph ("graphs/" + n + ".png", 8, PdfWriter::courier, juce::Colour (0xff666666));
        }
        pdf.newPage();
        mdToPdf (req);
        pdf.newPage();
        mdToPdf (val);
        if (! pdf.save (dir.getChildFile ("blueprint.pdf")))
            error = "PDF could not be written (all other files are complete).";
    }

    report (1.0, "Done.");
    return dir;
}

// ================================================================== dialog

namespace
{
    class Dialog : public juce::Component, private juce::Timer, private juce::Thread
    {
    public:
        Dialog (Ui& u, std::function<void()> close) : juce::Thread ("Atelier blueprint export"), ui (u), onClose (std::move (close))
        {
            nameEd.setText (ui.proc.projectName(), false);
            objectiveEd.setMultiLine (true, true);
            objectiveEd.setReturnKeyStartsNewLine (true);
            objectiveEd.setText (ui.proc.objective(), false);
            objectiveEd.setTextToShowWhenEmpty ("Describe the sound in your own words, e.g. 'warm, glued vocal with airy top and a short slap echo'.", theme::textDim);
            for (auto* e : { &nameEd, &objectiveEd })
            {
                e->setFont (theme::sans (14.0f));
                addAndMakeVisible (e);
            }
            rate.addItemList ({ "44.1 kHz", "48 kHz", "96 kHz" }, 1);
            rate.setSelectedItemIndex (ui.proc.currentSampleRate() > 90000 ? 2 : (ui.proc.currentSampleRate() < 46000 ? 0 : 1), juce::dontSendNotification);
            addAndMakeVisible (rate);
            audioToggle.setToggleState (true, juce::dontSendNotification);
            pdfToggle.setToggleState (true, juce::dontSendNotification);
            addAndMakeVisible (audioToggle);
            addAndMakeVisible (pdfToggle);
            folder = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
            folderBtn.onClick = [this] { chooseFolder(); };
            addAndMakeVisible (folderBtn);
            exportBtn.getProperties().set ("beige", true);
            exportBtn.onClick = [this] { start(); };
            cancelBtn.onClick = [this] { if (isThreadRunning()) signalThreadShouldExit(); else if (onClose) onClose(); };
            openBtn.onClick = [this] { result.revealToUser(); };
            addAndMakeVisible (exportBtn);
            addAndMakeVisible (cancelBtn);
            addChildComponent (openBtn);
            startTimerHz (10);
        }

        ~Dialog() override { stopThread (10000); }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (juce::Colour (0xd0050709));
            auto r = box();
            g.setColour (theme::shadow);
            g.fillRect (r.translated (6, 6));
            theme::drawPanel (g, r, "Export blueprint", "Fig. 05 - engineering package for a new plugin");
            g.setColour (theme::textDim);
            g.setFont (theme::mono (9.5f));
            auto in = r.reduced (24, 0);
            g.drawText ("PROJECT NAME", in.getX(), nameEd.getY() - 18, 300, 14, juce::Justification::centredLeft);
            g.drawText ("SONIC OBJECTIVE", in.getX(), objectiveEd.getY() - 18, 300, 14, juce::Justification::centredLeft);
            g.drawText ("RENDER RATE", in.getX(), rate.getY() - 18, 300, 14, juce::Justification::centredLeft);
            g.drawText ("DESTINATION", folderBtn.getX(), folderBtn.getY() - 18, 300, 14, juce::Justification::centredLeft);
            g.setColour (theme::text);
            g.setFont (theme::sans (12.5f));
            g.drawText (folder.getChildFile ("Atelier_Export").getFullPathName(), folderBtn.getBounds().translated (0, 30).withWidth (in.getRight() - folderBtn.getX()),
                        juce::Justification::centredLeft, true);

            auto pr = juce::Rectangle<int> (in.getX(), exportBtn.getY() - 64, in.getWidth(), 6);
            g.setColour (theme::grid);
            g.fillRect (pr);
            g.setColour (theme::beige);
            g.fillRect (pr.withWidth ((int) (pr.getWidth() * progress.load())));
            g.setColour (theme::textDim);
            g.setFont (theme::sans (12.5f));
            const juce::ScopedLock sl (lock);
            g.drawFittedText (status, pr.translated (0, 12).withHeight (36), juce::Justification::topLeft, 2);
        }

        void resized() override
        {
            auto in = box().reduced (24, 0).withTrimmedTop (72);
            nameEd.setBounds (in.removeFromTop (30));
            in.removeFromTop (30);
            objectiveEd.setBounds (in.removeFromTop (90));
            in.removeFromTop (30);
            auto row = in.removeFromTop (28);
            rate.setBounds (row.removeFromLeft (140));
            row.removeFromLeft (30);
            folderBtn.setBounds (row.removeFromLeft (160));
            in.removeFromTop (46);
            audioToggle.setBounds (in.removeFromTop (24).withWidth (420));
            pdfToggle.setBounds (in.removeFromTop (24).withWidth (420));
            auto b = box().reduced (24, 20).removeFromBottom (36);
            exportBtn.setBounds (b.removeFromRight (180));
            b.removeFromRight (8);
            cancelBtn.setBounds (b.removeFromRight (110));
            openBtn.setBounds (b.removeFromLeft (180));
        }

    private:
        juce::Rectangle<int> box() const { return getLocalBounds().withSizeKeepingCentre (720, 600); }

        void chooseFolder()
        {
            chooser = std::make_unique<juce::FileChooser> ("Export destination", folder);
            juce::Component::SafePointer<Dialog> safe (this);
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [safe] (const juce::FileChooser& fc)
            {
                if (safe != nullptr && fc.getResult() != juce::File()) { safe->folder = fc.getResult(); safe->repaint(); }
            });
        }

        void start()
        {
            if (isThreadRunning()) return;
            ui.proc.setProjectName (nameEd.getText().trim().isEmpty() ? "Untitled study" : nameEd.getText().trim());
            ui.proc.setObjective (objectiveEd.getText().trim());
            opt.projectName = ui.proc.projectName();
            opt.objective = ui.proc.objective();
            const double rates[] = { 44100.0, 48000.0, 96000.0 };
            opt.sampleRate = rates[rate.getSelectedItemIndex()];
            opt.includeAudio = audioToggle.getToggleState();
            opt.includePdf = pdfToggle.getToggleState();
            opt.baseFolder = folder;
            bridge = ui.proc.bridgeToTree().createCopy();
            userAudio = ui.proc.currentFile();
            exportBtn.setEnabled (false);
            openBtn.setVisible (false);
            progress = 0.0;
            startThread (juce::Thread::Priority::normal);
        }

        void run() override
        {
            juce::String err;
            auto dir = exportPackage (bridge, opt, userAudio,
                                      [this] (double p, const juce::String& s) { progress = p; const juce::ScopedLock sl (lock); status = s; },
                                      [this] { return threadShouldExit(); }, err);
            const juce::ScopedLock sl (lock);
            result = dir;
            status = dir.exists() && err.isEmpty() ? "Exported to " + dir.getFullPathName() + "  -  give this folder to Claude."
                                                   : (err.isNotEmpty() ? err : juce::String ("Export failed."));
            finished = true;
        }

        void timerCallback() override
        {
            if (finished.exchange (false))
            {
                exportBtn.setEnabled (true);
                openBtn.setVisible (result.exists());
            }
            cancelBtn.setButtonText (isThreadRunning() ? "STOP" : "CLOSE");
            repaint();
        }

        Ui& ui;
        std::function<void()> onClose;
        juce::TextEditor nameEd, objectiveEd;
        juce::ComboBox rate;
        juce::ToggleButton audioToggle { "Include audio references (WAV)" }, pdfToggle { "Include PDF document" };
        juce::TextButton folderBtn { "CHOOSE FOLDER..." }, exportBtn { "EXPORT \"BLUEPRINT\"" }, cancelBtn { "CLOSE" }, openBtn { "SHOW IN FOLDER" };
        std::unique_ptr<juce::FileChooser> chooser;
        juce::File folder, result, userAudio;
        Options opt;
        juce::ValueTree bridge;
        std::atomic<double> progress { 0.0 };
        std::atomic<bool> finished { false };
        juce::CriticalSection lock;
        juce::String status { "Atelier renders the saved bridge offline, measures it and writes the package. This takes up to a minute." };
    };
}

std::unique_ptr<juce::Component> createDialog (Ui& ui, std::function<void()> onClose)
{
    return std::make_unique<Dialog> (ui, std::move (onClose));
}

} // namespace Blueprint
