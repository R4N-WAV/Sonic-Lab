#include "ModuleEditor.h"

#include <sonic/modules/EqModule.h>
#include <sonic/modules/TubeModule.h>

ModuleEditor::ModuleEditor (AtelierProcessor& p) : proc (p)
{
    description.setFont (theme::label (12.0f));
    description.setColour (juce::Label::textColourId, theme::textDim);
    description.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (description);
    setModule (0);
    startTimerHz (20);
}

void ModuleEditor::setModule (int index)
{
    moduleIndex = index;
    controls.clear();
    auto& mod = proc.chain().module (index);
    for (const auto& s : mod.params())
    {
        const auto id = AtelierProcessor::paramId (index, s.id);
        juce::Component* c = s.isChoice()
            ? (juce::Component*) new ParamCombo (proc.apvts, id, s.name, s.description)
            : (juce::Component*) new ParamSlider (proc.apvts, id, s.name, s.description);
        addAndMakeVisible (controls.add (c));
    }

    if (std::string (mod.typeId()) == "tube.stage")
        description.setText ("Oversampled tube stage: Miller input low-pass, biased asymmetric nonlinearity with grid-current "
                             "bias shift (sag), coupling-capacitor DC block. Hover a control for details.",
                             juce::dontSendNotification);
    else
        description.setText ("Five-section EQ (RBJ biquads, 64-bit). The curve is computed from the exact coefficients used for audio.",
                             juce::dontSendNotification);
    resized();
    repaint();
}

void ModuleEditor::timerCallback()
{
    repaint (graphArea);
}

void ModuleEditor::paint (juce::Graphics& g)
{
    auto& mod = proc.chain().module (moduleIndex);
    theme::drawPanel (g, getLocalBounds(), juce::String ("Stage  /  ") + mod.displayName());

    auto r = graphArea.toFloat();
    g.setColour (theme::bg);
    g.fillRoundedRectangle (r, 2.0f);
    const auto plotArea = r.reduced (50.0f, 14.0f).withTrimmedBottom (10.0f);

    if (std::string (mod.typeId()) == "tube.stage")
        drawTubeGraph (g, plotArea);
    else
        drawEqGraph (g, plotArea);
}

void ModuleEditor::drawTubeGraph (juce::Graphics& g, juce::Rectangle<float> r)
{
    auto& tube = static_cast<sonic::TubeModule&> (proc.chain().module (moduleIndex));

    // Axes: input -1..1 full scale, output auto-scaled
    double yMax = 0.05;
    constexpr int N = 400;
    std::array<double, N + 1> ys {};
    for (int i = 0; i <= N; ++i)
    {
        const double x = -1.0 + 2.0 * i / N;
        ys[(size_t) i] = tube.staticCurve (x);
        yMax = juce::jmax (yMax, std::abs (ys[(size_t) i]));
    }
    yMax = juce::jmin (yMax * 1.1, 4.0);

    auto toPt = [&] (double x, double y)
    {
        return juce::Point<float> (r.getX() + (float) ((x + 1.0) * 0.5) * r.getWidth(),
                                   r.getCentreY() - (float) (y / yMax) * r.getHeight() * 0.5f);
    };

    g.setFont (theme::mono (9.5f));
    for (double x : { -1.0, -0.5, 0.0, 0.5, 1.0 })
    {
        const auto p = toPt (x, 0.0);
        g.setColour (x == 0.0 ? theme::metal : theme::grid);
        g.drawVerticalLine ((int) p.x, r.getY(), r.getBottom());
        g.setColour (theme::textFaint);
        g.drawText (juce::String (x, 1), juce::Rectangle<float> (p.x - 20.0f, r.getBottom() + 2.0f, 40.0f, 12.0f), juce::Justification::centred);
    }
    for (double y : { -1.0, -0.5, 0.0, 0.5, 1.0 })
    {
        if (std::abs (y) > yMax) continue;
        const auto p = toPt (0.0, y);
        g.setColour (y == 0.0 ? theme::metal : theme::grid);
        g.drawHorizontalLine ((int) p.y, r.getX(), r.getRight());
        g.setColour (theme::textFaint);
        g.drawText (juce::String (y, 1), juce::Rectangle<float> (r.getX() - 46.0f, p.y - 6.0f, 42.0f, 12.0f), juce::Justification::centredRight);
    }

    // Identity reference (unity gain, no distortion)
    g.setColour (theme::reference.withAlpha (0.5f));
    const double m = juce::jmin (1.0, yMax);
    g.drawLine (juce::Line<float> (toPt (-m, -m), toPt (m, m)), 1.0f);

    juce::Path path;
    for (int i = 0; i <= N; ++i)
    {
        const auto p = toPt (-1.0 + 2.0 * i / N, ys[(size_t) i]);
        if (i == 0) path.startNewSubPath (p); else path.lineTo (p);
    }
    g.setColour (theme::accent);
    g.strokePath (path, juce::PathStrokeType (1.8f));

    g.setColour (theme::textDim);
    g.setFont (theme::label (11.0f));
    g.drawText ("Static transfer curve: input (full scale) -> output. Sag and filtering are dynamic and not shown.",
                r.withHeight (14.0f).translated (0.0f, -12.0f), juce::Justification::centredLeft);
}

void ModuleEditor::drawEqGraph (juce::Graphics& g, juce::Rectangle<float> r)
{
    auto& eq = static_cast<sonic::EqModule&> (proc.chain().module (moduleIndex));
    const double fs = proc.currentSampleRate();
    const auto design = eq.currentDesign (fs);

    plot::drawFreqGrid (g, r, -18.0, 18.0, 6.0);

    juce::Path mag, ph;
    const int N = juce::jmax (100, (int) r.getWidth());
    const double fMax = juce::jmin (20000.0, fs * 0.499);
    for (int i = 0; i <= N; ++i)
    {
        const double f = 20.0 * std::pow (fMax / 20.0, (double) i / N);
        const float x = plot::freqToX (f, r);
        const double db = juce::jlimit (-18.0, 18.0, juce::Decibels::gainToDecibels (sonic::EqModule::magnitudeAt (design, fs, f), -60.0));
        const double deg = juce::radiansToDegrees (sonic::EqModule::phaseAt (design, fs, f));
        const float ym = plot::dbToY (db, r, -18.0, 18.0);
        const float yp = plot::dbToY (deg / 10.0, r, -18.0, 18.0); // phase: 10 deg per dB grid unit (+-180 deg)
        if (i == 0) { mag.startNewSubPath (x, ym); ph.startNewSubPath (x, yp); }
        else { mag.lineTo (x, ym); ph.lineTo (x, yp); }
    }
    g.setColour (theme::reference.withAlpha (0.55f));
    g.strokePath (ph, juce::PathStrokeType (1.0f));
    g.setColour (theme::accent);
    g.strokePath (mag, juce::PathStrokeType (1.8f));

    g.setColour (theme::textDim);
    g.setFont (theme::label (11.0f));
    g.drawText ("Magnitude (violet, dB)   Phase (grey, 10 deg per grid dB)   at " + juce::String (fs / 1000.0, 1) + " kHz",
                r.withHeight (14.0f).translated (0.0f, -12.0f), juce::Justification::centredLeft);
}

void ModuleEditor::resized()
{
    auto r = getLocalBounds().reduced (12);
    r.removeFromTop (22);
    auto left = r.removeFromLeft (juce::jmin (420, r.getWidth() / 2));
    r.removeFromLeft (12);
    graphArea = r;

    description.setBounds (left.removeFromBottom (44));
    for (auto* c : controls)
        c->setBounds (left.removeFromTop (28));
}
