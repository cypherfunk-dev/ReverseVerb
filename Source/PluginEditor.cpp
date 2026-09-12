#include "PluginEditor.h"

namespace col
{
    // Paleta de agujero de gusano: violeta profundo de fondo, cian y magenta
    // como acentos. Los knobs interpolan de cian a magenta segun su valor, asi
    // que el color tambien informa: de un vistazo ves que esta alto y que bajo.
    const juce::Colour space   { 0xff07040f };
    const juce::Colour deep    { 0xff1c1038 };
    const juce::Colour panel   { 0xff150e26 };
    const juce::Colour cyan    { 0xff5ee7ff };
    const juce::Colour magenta { 0xffff5ecb };
    const juce::Colour amber   { 0xffffb35e };
    const juce::Colour violet  { 0xffa06bff };
    const juce::Colour text    { 0xffe2daf5 };
    const juce::Colour dim     { 0xff7a6b99 };
}

namespace lay
{
    // Tamano de diseno. Todo se posiciona en estas coordenadas y luego el
    // editor escala el contenedor entero, asi que crecer en alto no rompe nada:
    // la ventana es redimensionable con relacion de aspecto fija.
    constexpr int W = 780, H = 840;
    constexpr int margin = 16;
    constexpr int cols   = 6;

    constexpr int viewY = 54, viewH = 130;   // boca del agujero de gusano

    constexpr int secRevY = 194;   // cabecera de seccion
    constexpr int rowRevY = 216;   // etiqueta de los knobs
    constexpr int ctlRevY = 326;

    constexpr int secDlyY = 360;
    constexpr int rowDlyY = 382;
    constexpr int ctlDlyY = 492;

    constexpr int secTapY = 526;   // TAPE no necesita fila de controles
    constexpr int rowTapY = 548;

    constexpr int secOutY = 666;
    constexpr int rowOutY = 688;
    constexpr int ctlOutY = 798;

    constexpr int knobH = 90;

    inline int colW()      { return (W - 2 * margin) / cols; }
    inline int colX (int i){ return margin + i * colW(); }
}

//==============================================================================
KnobLNF::KnobLNF()
{
    setColour (juce::Label::textColourId,            col::text);
    setColour (juce::Slider::textBoxTextColourId,    col::text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::ComboBox::backgroundColourId,   col::panel);
    setColour (juce::ComboBox::textColourId,         col::text);
    setColour (juce::ComboBox::outlineColourId,      col::dim);
    setColour (juce::ComboBox::arrowColourId,        col::dim);
    setColour (juce::ToggleButton::textColourId,     col::text);
    setColour (juce::ToggleButton::tickColourId,     col::cyan);
    setColour (juce::ToggleButton::tickDisabledColourId, col::dim);
    setColour (juce::PopupMenu::backgroundColourId,  col::panel);
    setColour (juce::PopupMenu::textColourId,        col::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, col::violet.withAlpha (0.40f));
    setColour (juce::TextButton::buttonColourId,     col::panel.brighter (0.15f));
    setColour (juce::TextButton::textColourOffId,    col::text);
    setColour (juce::AlertWindow::backgroundColourId, col::panel);
    setColour (juce::AlertWindow::textColourId,       col::text);
}

void KnobLNF::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h,
                                float pos, float a0, float a1, juce::Slider& s)
{
    const auto area   = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (5.0f);
    const auto radius = juce::jmin (area.getWidth(), area.getHeight()) * 0.5f;
    const auto centre = area.getCentre();
    const auto angle  = a0 + pos * (a1 - a0);
    const float thick = radius * 0.22f;
    const bool  on    = s.isEnabled();

    juce::Path bg;
    bg.addCentredArc (centre.x, centre.y, radius - thick * 0.5f, radius - thick * 0.5f,
                      0.0f, a0, a1, true);
    g.setColour (col::violet.withAlpha (0.20f));
    g.strokePath (bg, juce::PathStrokeType (thick, juce::PathStrokeType::curved,
                                            juce::PathStrokeType::rounded));

    if (pos > 0.001f)
    {
        // El color va de cian (bajo) a magenta (alto): informa del valor sin
        // tener que leer el numero.
        auto c = col::cyan.interpolatedWith (col::magenta, pos);
        if (! on) c = col::dim;
        else if (s.isMouseOverOrDragging()) c = c.brighter (0.35f);

        juce::Path val;
        val.addCentredArc (centre.x, centre.y, radius - thick * 0.5f, radius - thick * 0.5f,
                           0.0f, a0, angle, true);

        // Bloom: el mismo arco mas grueso y casi transparente debajo.
        g.setColour (c.withAlpha (0.22f));
        g.strokePath (val, juce::PathStrokeType (thick * 2.1f, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));
        g.setColour (c);
        g.strokePath (val, juce::PathStrokeType (thick, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));
    }

    juce::Path ptr;
    ptr.startNewSubPath (0.0f, -radius * 0.30f);
    ptr.lineTo          (0.0f, -radius * 0.80f);
    g.setColour (on ? col::text : col::dim);
    g.strokePath (ptr, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded),
                  juce::AffineTransform::rotation (angle).translated (centre));
}

//==============================================================================
WormholeView::WormholeView (ReverseVerbProcessor& p) : proc (p)
{
    pFeedback = proc.apvts.getRawParameterValue ("feedback");
    pRevAmt   = proc.apvts.getRawParameterValue ("revamt");
    pFreeze   = proc.apvts.getRawParameterValue ("freeze");
    startTimerHz (30);
}

void WormholeView::timerCallback()
{
    // El tunel avanza solo; el feedback lo acelera. Se envuelve en 0..1 para
    // que el float no crezca sin limite y pierda precision tras horas abierto.
    const float fb = pFeedback != nullptr ? pFeedback->load() * 0.01f : 0.0f;

    // Congelado, el tunel se para. La metafora se sostiene sola: el buffer deja
    // de avanzar y la imagen tambien.
    if (! proc.visFrozen.load())
    {
        tunnel += 0.004f + 0.010f * fb;
        if (tunnel >= 1.0f) tunnel -= 1.0f;
    }

    slow += 0.011f;
    if (slow >= juce::MathConstants<float>::twoPi) slow -= juce::MathConstants<float>::twoPi;

    repaint();
}

void WormholeView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    const float cx = r.getCentreX(), cy = r.getCentreY();

    g.setColour (col::space);
    g.fillRoundedRectangle (r, 6.0f);
    g.reduceClipRegion (r.getSmallestIntegerContainer());

    const float fb     = pFeedback != nullptr ? pFeedback->load() * 0.01f : 0.0f;
    const float rev    = pRevAmt   != nullptr ? pRevAmt->load()   * 0.01f : 0.0f;
    const float phase  = proc.visPhase.load();
    const float duck   = proc.visDuckGain.load();

    const float maxW = r.getWidth()  * 0.94f;
    const float maxH = r.getHeight() * 0.94f;

    // --- el tunel ---------------------------------------------------------
    constexpr int rings = 18;

    for (int i = 0; i < rings; ++i)
    {
        float d = static_cast<float> (i) / rings + tunnel;
        if (d >= 1.0f) d -= 1.0f;

        // pow(d, 1.7) concentra anillos cerca del centro: da la sensacion de
        // profundidad sin necesidad de proyeccion real.
        const float k = std::pow (d, 1.7f);
        const float w = maxW * k;
        const float h = maxH * k;

        // Nacen y mueren en alpha 0, igual que las ventanas de grano: es el
        // mismo truco para que no se vea aparecer nada de golpe.
        const float alpha = std::sin (juce::MathConstants<float>::pi * d);

        // La torsion sigue al feedback: mas cola, tunel mas retorcido.
        const float bendX = std::sin (d * 3.4f + slow)        * r.getWidth()  * 0.09f * (0.3f + fb);
        const float bendY = std::cos (d * 2.1f + slow * 0.7f) * r.getHeight() * 0.11f * (0.3f + fb);

        auto c = col::cyan.interpolatedWith (col::magenta, d);

        juce::Path p;
        p.addEllipse (cx + bendX - w * 0.5f, cy + bendY - h * 0.5f, w, h);
        g.setColour (c.withAlpha (alpha * 0.42f * duck));
        g.strokePath (p, juce::PathStrokeType (1.4f));
    }

    // --- nucleo: halo segun el reverb -------------------------------------
    if (rev > 0.01f)
    {
        const float gr = 26.0f * rev;
        juce::ColourGradient halo (col::violet.withAlpha (0.45f * rev * duck), cx, cy,
                                   col::violet.withAlpha (0.0f), cx + gr, cy, true);
        g.setGradientFill (halo);
        g.fillEllipse (cx - gr, cy - gr, gr * 2.0f, gr * 2.0f);
    }

    // --- los dos granos ---------------------------------------------------
    // Esto NO es adorno: el brillo es sin(pi*fase), la ganancia real de la
    // ventana de cada grano. Se ven alternar y cruzarse a media intensidad.
    auto drawGrain = [&] (float ph, juce::Colour c)
    {
        const float gain = std::sin (juce::MathConstants<float>::pi * ph);
        const float a    = juce::MathConstants<float>::twoPi * ph;
        const float x    = cx + std::cos (a) * maxW * 0.40f;
        const float y    = cy + std::sin (a) * maxH * 0.40f;

        g.setColour (c.withAlpha (0.10f + 0.30f * gain));
        g.fillEllipse (x - 11.0f, y - 11.0f, 22.0f, 22.0f);
        g.setColour (c.withAlpha (0.25f + 0.75f * gain));
        g.fillEllipse (x - 3.0f, y - 3.0f, 6.0f, 6.0f);
    };

    float phB = phase + 0.5f;
    if (phB >= 1.0f) phB -= 1.0f;

    drawGrain (phase, col::cyan);
    drawGrain (phB,   col::magenta);

    // --- texto ------------------------------------------------------------
    g.setColour (col::dim);
    g.setFont (juce::FontOptions (11.0f));

    juce::String left = "rev " + juce::String (proc.visLengthMs.load(), 0) + " ms"
                      + "   dly " + juce::String (proc.visDelayMs.load(), 0) + " ms";
    if (duck < 0.98f)
        left += "   duck -" + juce::String (-20.0f * std::log10 (juce::jmax (0.001f, duck)), 1) + " dB";
    const float dv = proc.visDrive.load();
    if (dv > 1.05f)
        left += "   drive " + juce::String (dv, 1);
    g.drawText (left, r.reduced (10.0f, 5.0f), juce::Justification::topLeft);

    juce::String right;
    if (proc.visFrozen.load())
        right << "FREEZE   ";
    if (! proc.visStereo.load())
        right << "MONO   ";

    const float bpm = proc.visBpm.load();
    if (bpm > 0.0f)
        right << (proc.visHostTempo.load() ? "host " : "manual ")
              << juce::String (bpm, 1) << " BPM";

    if (right.isNotEmpty())
        g.drawText (right, r.reduced (10.0f, 5.0f), juce::Justification::topRight);
}

//==============================================================================
Meters::Meters (ReverseVerbProcessor& p) : proc (p) { startTimerHz (20); }

void Meters::drawBar (juce::Graphics& g, juce::Rectangle<float> r,
                      const char* label, float peak)
{
    g.setColour (col::dim);
    g.setFont (juce::FontOptions (10.0f));
    g.drawText (label, r.removeFromLeft (26.0f), juce::Justification::centredLeft);

    auto db = r.removeFromRight (44.0f);

    g.setColour (col::panel.brighter (0.25f));
    g.fillRoundedRectangle (r, 2.0f);

    // Escala de -48 a +6 dB. Por encima de 0 dBFS el tramo se pinta en ambar:
    // no es un limitador, es un aviso.
    const float dbVal = 20.0f * std::log10 (juce::jmax (1.0e-4f, peak));
    const float norm  = juce::jlimit (0.0f, 1.0f, (dbVal + 48.0f) / 54.0f);
    const float zero  = 48.0f / 54.0f;

    auto bar = r.withWidth (r.getWidth() * norm);
    g.setColour (dbVal > 0.0f ? col::amber : col::cyan.withAlpha (0.85f));
    g.fillRoundedRectangle (bar, 2.0f);

    // marca de 0 dBFS
    const float zx = r.getX() + r.getWidth() * zero;
    g.setColour (col::text.withAlpha (0.35f));
    g.drawLine (zx, r.getY(), zx, r.getBottom(), 1.0f);

    g.setColour (dbVal > 0.0f ? col::amber : col::dim);
    g.setFont (juce::FontOptions (10.0f));
    g.drawText (peak > 1.0e-4f ? juce::String (dbVal, 1) + " dB" : "-inf",
                db, juce::Justification::centredRight);
}

void Meters::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    const float h = r.getHeight() * 0.5f;

    drawBar (g, r.removeFromTop (h).reduced (0.0f, 2.0f), "IN",  proc.visInPeak.load());
    drawBar (g, r.reduced (0.0f, 2.0f),                   "OUT", proc.visOutPeak.load());
}

//==============================================================================
void BackPanel::paint (juce::Graphics& g)
{
    // Gradiente radial centrado en la boca del tunel. Es estatico: este
    // componente solo repinta al redimensionar, no en cada frame.
    juce::ColourGradient bg (col::deep,  lay::W * 0.5f, static_cast<float> (lay::viewY + lay::viewH / 2),
                             col::space, lay::W * 0.5f, lay::H * 1.15f, true);
    g.setGradientFill (bg);
    g.fillAll();

    g.setColour (col::text);
    g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    g.drawText ("ReverseVerb", lay::margin, 10, 270, 26, juce::Justification::centredLeft);

    g.setColour (col::violet);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText ("horizonte de sucesos", lay::margin, 32, 270, 16,
                juce::Justification::centredLeft);

    // Las cabeceras se quedan con nombres funcionales a proposito: la
    // ambientacion va en los graficos, no en renombrar controles que luego no
    // sabrias buscar.
    auto header = [&] (const char* name, int x, int y, int w)
    {
        g.setColour (col::cyan.withAlpha (0.85f));
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (name, x, y, w, 14, juce::Justification::centredLeft);
    };

    auto rule = [&] (int y)
    {
        juce::ColourGradient lineGrad (col::violet.withAlpha (0.55f), static_cast<float> (lay::margin), 0.0f,
                                       col::violet.withAlpha (0.0f),  static_cast<float> (lay::W - lay::margin), 0.0f,
                                       false);
        g.setGradientFill (lineGrad);
        g.fillRect (lay::margin, y + 16, lay::W - 2 * lay::margin, 1);
    };

    header ("REVERSE   (drive y filtros actuan dentro de su lazo)", lay::margin, lay::secRevY, 520);
    rule (lay::secRevY);

    header ("DELAY", lay::margin, lay::secDlyY, 300);
    rule (lay::secDlyY);

    header ("TAPE Y PITCH   (actuan sobre el motor de reverse)", lay::margin, lay::secTapY, 460);
    rule (lay::secTapY);

    header ("SPACE",  lay::margin,   lay::secOutY, 200);
    header ("OUTPUT", lay::colX (3), lay::secOutY, 200);
    rule (lay::secOutY);

    g.setColour (col::violet.withAlpha (0.35f));
    g.drawLine (static_cast<float> (lay::colX (3) - 8), static_cast<float> (lay::secOutY + 22),
                static_cast<float> (lay::colX (3) - 8), static_cast<float> (lay::ctlOutY - 8), 1.0f);
}

//==============================================================================
ReverseVerbEditor::ReverseVerbEditor (ReverseVerbProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (content);

    wormhole = std::make_unique<WormholeView> (proc);
    content.addAndMakeVisible (*wormhole);

    meters = std::make_unique<Meters> (proc);
    content.addAndMakeVisible (*meters);

    addKnob (length,  "length",   "Length");
    addKnob (revFb,   "feedback", "Feedback");
    addKnob (drive,   "drive",    "Drive");
    addKnob (driveEnv,"driveenv", "Drive Env");
    addKnob (revLow,  "highpass", "Low Cut");
    addKnob (revHigh, "lowpass",  "High Cut");

    addKnob (dTime,   "dtime",    "Time");
    addKnob (dFb,     "dfeed",    "Feedback");
    addKnob (dLow,    "dhigh",    "Low Cut");
    addKnob (dHigh,   "dlow",     "High Cut");
    addKnob (dMod,    "dmoddepth","Mod");
    addKnob (dModRate,"dmodrate", "Mod Rate");

    addKnob (wow,     "wow",      "Wow");
    addKnob (flutter, "flutter",  "Flutter");
    addKnob (detune,  "detune",   "Detune");
    addKnob (shimmer, "shimmer",  "Shimmer");
    addKnob (shimPitch,"shimpitch","Shim Pitch");

    addKnob (revAmt,  "revamt",   "Reverb");
    addKnob (revSize, "revsize",  "Size");
    addKnob (revDamp, "revdamp",  "Damp");
    addKnob (mix,     "mix",      "Mix");
    addKnob (duck,    "duck",     "Duck");
    addKnob (duckRel, "duckrel",  "Duck Rel");

    for (auto* c : { &syncButton, &dSyncButton, &pingButton, &postButton, &freezeButton })
        content.addAndMakeVisible (c);

    for (auto* c : { &divisionBox, &dDivisionBox, &routingBox })
        content.addAndMakeVisible (c);

    divisionBox .addItemList (ReverseVerbProcessor::divisionNames(), 1);
    dDivisionBox.addItemList (ReverseVerbProcessor::divisionNames(), 1);
    routingBox  .addItemList (ReverseVerbProcessor::routingNames(),  1);

    // --- tempo manual ---
    tempoSlider.setSliderStyle (juce::Slider::LinearBar);
    tempoSlider.setTextValueSuffix (" BPM");
    tempoSlider.setNumDecimalPlacesToDisplay (1);
    content.addAndMakeVisible (tempoSlider);
    tempoLabel.setText ("Tempo", juce::dontSendNotification);
    tempoLabel.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    tempoLabel.setJustificationType (juce::Justification::centredRight);
    content.addAndMakeVisible (tempoLabel);
    tempoAtt = std::make_unique<SliderAtt> (proc.apvts, "tempo", tempoSlider);

    syncAtt  = std::make_unique<ButtonAtt> (proc.apvts, "sync",     syncButton);
    dSyncAtt = std::make_unique<ButtonAtt> (proc.apvts, "dsync",    dSyncButton);
    pingAtt  = std::make_unique<ButtonAtt> (proc.apvts, "pingpong", pingButton);
    postAtt  = std::make_unique<ButtonAtt> (proc.apvts, "revpost",  postButton);
    freezeAtt= std::make_unique<ButtonAtt> (proc.apvts, "freeze",   freezeButton);

    divAtt   = std::make_unique<ComboAtt> (proc.apvts, "division", divisionBox);
    dDivAtt  = std::make_unique<ComboAtt> (proc.apvts, "ddiv",     dDivisionBox);
    routeAtt = std::make_unique<ComboAtt> (proc.apvts, "routing",  routingBox);

    syncButton  .onClick = [this] { updateEnablement(); };
    freezeButton.onClick = [this] { updateEnablement(); };
    dSyncButton.onClick  = [this] { updateEnablement(); };
    routingBox .onChange = [this] { updateEnablement(); };

    updateEnablement();
    startTimerHz (4);

    // --- barra de presets ---
    for (auto* b : { &prevBtn, &nextBtn, &saveBtn, &delBtn })
        content.addAndMakeVisible (b);

    content.addAndMakeVisible (presetBox);
    presetBox.setTextWhenNothingSelected (proc.getPresets().currentName);

    presetBox.onChange = [this]
    {
        const int id = presetBox.getSelectedId();
        if (id <= 0)
            return;

        if (id < userIdBase)
        {
            proc.applyFactoryPreset (id - 1);
        }
        else
        {
            const auto users = proc.getPresets().getUserPresetNames();
            const int  idx   = id - userIdBase;

            if (juce::isPositiveAndBelow (idx, users.size()))
                proc.getPresets().loadUserPreset (users[idx]);
        }
    };

    prevBtn.onClick = [this] { stepPreset (-1); };
    nextBtn.onClick = [this] { stepPreset ( 1); };
    saveBtn.onClick = [this] { showSaveDialog(); };
    delBtn .onClick = [this] { showDeleteDialog(); };

    refreshPresetList();

    setResizable (true, true);
    if (auto* c = getConstrainer())
    {
        c->setFixedAspectRatio (static_cast<double> (lay::W) / static_cast<double> (lay::H));
        c->setSizeLimits (lay::W * 7 / 10, lay::H * 7 / 10, lay::W * 8 / 5, lay::H * 8 / 5);
    }
    setSize (lay::W, lay::H);
}

ReverseVerbEditor::~ReverseVerbEditor()
{
    setLookAndFeel (nullptr);
}

void ReverseVerbEditor::addKnob (Knob& k, const juce::String& paramID, const juce::String& text)
{
    k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 72, 16);
    content.addAndMakeVisible (k.slider);

    k.label.setText (text, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    content.addAndMakeVisible (k.label);

    k.att = std::make_unique<SliderAtt> (proc.apvts, paramID, k.slider);
}

void ReverseVerbEditor::layoutRow (Knob* const* knobs, int count, int y)
{
    const int w = lay::colW();

    for (int i = 0; i < count; ++i)
    {
        const int x = lay::colX (i);
        knobs[i]->label.setBounds (x, y, w, 14);
        knobs[i]->slider.setBounds (x + w / 2 - 46, y + 16, 92, lay::knobH);
    }
}

void ReverseVerbEditor::updateEnablement()
{
    // Con Sync activo manda la division, no el slider de ms.
    const bool rs = syncButton.getToggleState();
    divisionBox.setEnabled (rs);

    // Congelado, el buffer no se escribe: Feedback y Length dejan de tener
    // efecto hasta descongelar. Mejor decirlo que dejar knobs que no hacen nada.
    const bool frozen = freezeButton.getToggleState();
    revFb.slider.setEnabled (! frozen);
    length.slider.setEnabled (! rs && ! frozen);

    const bool ds = dSyncButton.getToggleState();
    const bool on = routingBox.getSelectedItemIndex() > 0;

    for (auto* k : { &dTime, &dFb, &dLow, &dHigh, &dMod, &dModRate })
        k->slider.setEnabled (on && (k != &dTime || ! ds));

    dSyncButton .setEnabled (on);
    dDivisionBox.setEnabled (on && ds);

    // El ping-pong necesita dos canales. En una pista mono el boton se puede
    // pulsar pero no hace nada, asi que mejor decirlo que dejar al usuario
    // pensando que el efecto esta roto.
    const bool stereo = proc.visStereo.load();
    pingButton.setEnabled (on && stereo);
    pingButton.setButtonText (stereo ? "Ping-Pong" : "Ping-Pong (mono)");

    // Con host, el BPM lo pone el; el control manual desaparece para no
    // sugerir que se puede cambiar algo que no se puede.
    const bool manualTempo = ! proc.visHostTempo.load();
    tempoSlider.setVisible (manualTempo);
    tempoLabel .setVisible (manualTempo);
}

void ReverseVerbEditor::refreshPresetList (int idToSelect)
{
    presetBox.clear (juce::dontSendNotification);

    presetBox.addSectionHeading ("Fabrica");
    for (int i = 0; i < PresetManager::getNumFactoryPresets(); ++i)
        presetBox.addItem (PresetManager::getFactoryPresetName (i), i + 1);

    const auto users = proc.getPresets().getUserPresetNames();

    if (! users.isEmpty())
    {
        presetBox.addSeparator();
        presetBox.addSectionHeading ("Tuyos");

        for (int i = 0; i < users.size(); ++i)
            presetBox.addItem (users[i], userIdBase + i);
    }

    if (idToSelect > 0)
        presetBox.setSelectedId (idToSelect, juce::dontSendNotification);

    // Borrar solo tiene sentido sobre un preset de usuario: los de fabrica
    // viven en el binario y el indice de programa del host depende de ellos.
    delBtn.setEnabled (presetBox.getSelectedId() >= userIdBase);
}

void ReverseVerbEditor::syncPresetDisplay()
{
    const auto name  = proc.getPresets().currentName;
    const bool dirty = proc.getPresets().isDirty();

    if (name == shownName && dirty == shownDirty)
        return;

    shownName  = name;
    shownDirty = dirty;

    // Con cambios encima, el combo no "es" ningun preset: se deselecciona y
    // se muestra el nombre con asterisco como texto de fondo.
    if (dirty)
    {
        presetBox.setTextWhenNothingSelected (name + " *");
        presetBox.setSelectedId (0, juce::dontSendNotification);
        delBtn.setEnabled (false);
        return;
    }

    presetBox.setTextWhenNothingSelected (name);

    const int idToSelect = presetIdForName (name);
    presetBox.setSelectedId (idToSelect, juce::dontSendNotification);
    delBtn.setEnabled (idToSelect >= userIdBase);
}

int ReverseVerbEditor::presetIdForName (const juce::String& name) const
{
    for (int i = 0; i < PresetManager::getNumFactoryPresets(); ++i)
        if (PresetManager::getFactoryPresetName (i) == name)
            return i + 1;

    const int idx = proc.getPresets().getUserPresetNames().indexOf (name);
    return idx >= 0 ? userIdBase + idx : 0;
}

void ReverseVerbEditor::stepPreset (int delta)
{
    const int n = presetBox.getNumItems();
    if (n <= 0)
        return;

    int cur = presetBox.getSelectedItemIndex();
    if (cur < 0)   // modificado (nada seleccionado): partir del preset de debajo
        cur = presetBox.indexOfItemId (presetIdForName (proc.getPresets().currentName));

    int idx = cur + delta;
    idx = juce::jlimit (0, n - 1, idx);

    presetBox.setSelectedItemIndex (idx, juce::sendNotificationSync);
    delBtn.setEnabled (presetBox.getSelectedId() >= userIdBase);
}

void ReverseVerbEditor::showSaveDialog()
{
    dialog = std::make_unique<juce::AlertWindow> ("Guardar preset",
                                                  "Nombre del preset:",
                                                  juce::MessageBoxIconType::NoIcon);
    dialog->addTextEditor ("name", proc.getPresets().currentName);
    dialog->addButton ("Guardar",  1, juce::KeyPress (juce::KeyPress::returnKey));
    dialog->addButton ("Cancelar", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    // deleteWhenDismissed = false: la ventana es nuestra (unique_ptr), asi el
    // callback puede leer el texto sin tocar memoria liberada.
    dialog->enterModalState (true, juce::ModalCallbackFunction::create (
        [this] (int result)
        {
            if (result == 1 && dialog != nullptr)
            {
                const auto name = dialog->getTextEditorContents ("name");

                if (proc.getPresets().saveUserPreset (name))
                {
                    const auto users = proc.getPresets().getUserPresetNames();
                    const int  idx   = users.indexOf (juce::File::createLegalFileName (name.trim()));
                    refreshPresetList (idx >= 0 ? userIdBase + idx : 0);
                }
            }

            dialog.reset();
        }), false);
}

void ReverseVerbEditor::showDeleteDialog()
{
    const int id = presetBox.getSelectedId();
    if (id < userIdBase)
        return;

    const auto users = proc.getPresets().getUserPresetNames();
    const int  idx   = id - userIdBase;

    if (! juce::isPositiveAndBelow (idx, users.size()))
        return;

    const auto name = users[idx];

    juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::WarningIcon,
        "Borrar preset", "Se borrara \"" + name + "\" del disco.",
        "Borrar", "Cancelar", nullptr,
        juce::ModalCallbackFunction::create ([this, name] (int result)
        {
            if (result == 1)
            {
                proc.getPresets().deleteUserPreset (name);
                refreshPresetList();
            }
        }));
}

void ReverseVerbEditor::resized()
{
    // El contenedor siempre mide el tamano de diseno; la ventana lo escala.
    const float scale = static_cast<float> (getWidth()) / static_cast<float> (lay::W);
    content.setTransform (juce::AffineTransform::scale (scale));
    content.setBounds (0, 0, lay::W, lay::H);

    presetBox.setBounds (300, 12, 250, 24);
    prevBtn  .setBounds (556, 12,  26, 24);
    nextBtn  .setBounds (584, 12,  26, 24);
    saveBtn  .setBounds (616, 12,  70, 24);
    delBtn   .setBounds (690, 12,  66, 24);

    wormhole->setBounds (lay::margin, lay::viewY, lay::W - 2 * lay::margin, lay::viewH);

    Knob* rev[] = { &length, &revFb, &drive, &driveEnv, &revLow, &revHigh };
    Knob* dly[] = { &dTime, &dFb, &dLow, &dHigh, &dMod, &dModRate };
    Knob* tap[] = { &wow, &flutter, &detune, &shimmer, &shimPitch };
    Knob* out[] = { &revAmt, &revSize, &revDamp, &mix, &duck, &duckRel };

    layoutRow (rev, 6, lay::rowRevY);
    layoutRow (dly, 6, lay::rowDlyY);
    layoutRow (tap, 5, lay::rowTapY);
    layoutRow (out, 6, lay::rowOutY);

    syncButton  .setBounds (lay::margin,       lay::ctlRevY,  70, 22);
    divisionBox .setBounds (lay::margin + 74,  lay::ctlRevY,  90, 22);
    freezeButton.setBounds (lay::margin + 178, lay::ctlRevY,  90, 22);
    tempoLabel  .setBounds (lay::margin + 290, lay::ctlRevY,  48, 22);
    tempoSlider .setBounds (lay::margin + 344, lay::ctlRevY, 120, 22);

    routingBox  .setBounds (lay::margin,       lay::ctlDlyY, 180, 22);
    dSyncButton .setBounds (lay::margin + 194, lay::ctlDlyY,  70, 22);
    dDivisionBox.setBounds (lay::margin + 268, lay::ctlDlyY,  90, 22);
    pingButton  .setBounds (lay::margin + 372, lay::ctlDlyY, 110, 22);

    postButton.setBounds (lay::margin, lay::ctlOutY, 280, 22);
    meters->setBounds (320, lay::ctlOutY - 6, lay::W - 320 - lay::margin, 34);
}
