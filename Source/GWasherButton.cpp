#include "GWasherButton.h"

void GWasherButton::GrindstoneFootswitchLAF::drawButtonBackground(juce::Graphics& g,
    juce::Button& button, const juce::Colour&, bool isMouseOver, bool isButtonDown)
{
    const float cx = (float)button.getWidth()  * 0.5f;
    const float cy = (float)button.getHeight() * 0.5f;
    const float r  = (float)juce::jmin(button.getWidth(), button.getHeight()) * 0.5f - 2.0f;
    const float brightness = isButtonDown ? 0.70f : (isMouseOver ? 1.06f : 1.0f);
    juce::ColourGradient grad(
        juce::Colour(0xffE8E8E8).withMultipliedBrightness(brightness), cx-r*0.32f, cy-r*0.42f,
        juce::Colour(0xff888888).withMultipliedBrightness(brightness), cx+r*0.28f, cy+r*0.38f, true);
    grad.addColour(0.55, juce::Colour(0xffB2B2B2).withMultipliedBrightness(brightness));
    g.setGradientFill(grad);
    g.fillEllipse(cx-r, cy-r, r*2.0f, r*2.0f);
    g.setColour(juce::Colour(0xff3C3C3C));
    g.drawEllipse(cx-r, cy-r, r*2.0f, r*2.0f, 1.5f);
    g.setColour(juce::Colours::white.withAlpha(0.28f));
    g.drawEllipse(cx-r+2.0f, cy-r+2.0f, (r-2.0f)*2.0f, (r-2.0f)*2.0f, 0.7f);
}

GWasherButton::GWasherButton(const juce::File& svgFile_, float buttonSizeProportion_)
    : svgFile(svgFile_), buttonSizeProportion(buttonSizeProportion_)
{
    bypassButton.setLookAndFeel(&laf);
    bypassButton.setClickingTogglesState(true);
    bypassButton.setToggleState(true, juce::dontSendNotification);
    bypassButton.onClick = [this] {
        if (onBypassChanged)
            onBypassChanged(bypassButton.getToggleState());
        repaint();
    };
    addAndMakeVisible(bypassButton);
}

GWasherButton::GWasherButton(const char* svgXml, size_t svgXmlSize, float buttonSizeProportion_)
    : svgXmlData(juce::String::fromUTF8(svgXml, (int)svgXmlSize)),
      buttonSizeProportion(buttonSizeProportion_)
{
    bypassButton.setLookAndFeel(&laf);
    bypassButton.setClickingTogglesState(true);
    bypassButton.setToggleState(true, juce::dontSendNotification);
    bypassButton.onClick = [this] {
        if (onBypassChanged)
            onBypassChanged(bypassButton.getToggleState());
        repaint();
    };
    addAndMakeVisible(bypassButton);
}

GWasherButton::~GWasherButton()
{
    bypassButton.setLookAndFeel(nullptr);
}

void GWasherButton::setBypassState(bool active)
{
    bypassButton.setToggleState(active, juce::dontSendNotification);
    repaint();
}

bool GWasherButton::getBypassState() const
{
    return bypassButton.getToggleState();
}

void GWasherButton::buildCache()
{
    std::unique_ptr<juce::XmlElement> xml;

    if (svgXmlData.isNotEmpty())
    {
        xml = juce::XmlDocument::parse(svgXmlData);
    }
    else if (svgFile.existsAsFile())
    {
        xml = juce::XmlDocument::parse(svgFile);
    }

    if (!xml) return;
    auto gearDrawable = juce::Drawable::createFromSVG(*xml);
    if (!gearDrawable) return;
    gearDrawable->replaceColour(juce::Colours::black, juce::Colour(192, 192, 192));

    // Step 1: render at 4x
    const float displayH = (float)getHeight();
    const float kMult    = 4.0f;
    const float sc4x     = displayH * kMult / 893.0f;
    const int   rw       = juce::roundToInt(1844.0f * sc4x);
    const int   rh       = juce::roundToInt(displayH * kMult);
    juce::Image hiRes(juce::Image::ARGB, rw, rh, true);
    { juce::Graphics gHi(hiRes); gearDrawable->draw(gHi, 1.0f, juce::AffineTransform::scale(sc4x)); }

    // Step 2: BFS flood-fill
    const int totalPx = rw * rh;
    std::vector<bool> isExterior(totalPx, false);
    std::vector<int>  queue;
    queue.reserve(rw*2 + rh*2);
    auto enqueue = [&](int px, int py) {
        const int i = py*rw+px;
        if (!isExterior[i] && hiRes.getPixelAt(px,py).getAlpha() < 128) { isExterior[i]=true; queue.push_back(i); }
    };
    for (int px=0; px<rw; ++px) { enqueue(px,0); enqueue(px,rh-1); }
    for (int py=1; py<rh-1; ++py) { enqueue(0,py); enqueue(rw-1,py); }
    for (int qi=0; qi<(int)queue.size(); ++qi) {
        const int i=queue[qi], px=i%rw, py=i/rw;
        if (px>0) enqueue(px-1,py); if (px<rw-1) enqueue(px+1,py);
        if (py>0) enqueue(px,py-1); if (py<rh-1) enqueue(px,py+1);
    }

    // Step 3: fill enclosed pixels
    for (int py=0; py<rh; ++py)
        for (int px=0; px<rw; ++px) {
            const int i=py*rw+px;
            if (!isExterior[i] && hiRes.getPixelAt(px,py).getAlpha()<128)
                hiRes.setPixelAt(px,py, juce::Colour(192,192,192));
        }

    // Step 4: downsample
    const float sc1x = displayH/893.0f;
    const int imgW = juce::roundToInt(1844.0f*sc1x);
    const int imgH = juce::roundToInt(displayH);
    cachedGearImage = juce::Image(juce::Image::ARGB, imgW, imgH, true);
    { juce::Graphics gFinal(cachedGearImage); gFinal.setImageResamplingQuality(juce::Graphics::highResamplingQuality); gFinal.drawImage(hiRes,0,0,imgW,imgH,0,0,rw,rh); }

    // Step 5: brushed texture
    { juce::Random rng(17);
      for (int py=0; py<imgH; ++py) {
        const float t=rng.nextFloat(), la=(t>0.76f)?0.055f:(t<0.10f?-0.055f:0.0f);
        for (int px=0; px<imgW; ++px) {
            const auto pixel=cachedGearImage.getPixelAt(px,py);
            if (pixel.getAlpha()<128) continue;
            const float gx=(float)px/(float)imgW-0.5f, gy=(float)py/(float)imgH-0.5f;
            const float bright=0.82f-gx*0.14f-gy*0.18f+la;
            const auto v=(uint8_t)juce::jlimit(0,255,(int)(210.0f*bright));
            cachedGearImage.setPixelAt(px,py,juce::Colour(v,v,v).withAlpha(pixel.getAlpha()));
        }
      }
    }
}

void GWasherButton::paint(juce::Graphics& g)
{
    if (cachedGearImage.isValid())
    {
        const float btnCx = (float)getWidth()  * 0.5f;
        const float btnCy = (float)getHeight() * 0.5f;
        const int imgW = cachedGearImage.getWidth();
        const int imgH = cachedGearImage.getHeight();
        g.drawImage(cachedGearImage,
                    juce::roundToInt(btnCx - imgW*0.5f),
                    juce::roundToInt(btnCy - imgH*0.5f),
                    imgW, imgH,
                    0, 0, imgW, imgH);
    }
}

void GWasherButton::resized()
{
    if (getHeight() != cachedHeight)
    {
        cachedHeight = getHeight();
        buildCache();
    }

    const int btnSize = juce::roundToInt(buttonSizeProportion * (float)getHeight());
    bypassButton.setBounds((getWidth()  - btnSize) / 2,
                           (getHeight() - btnSize) / 2,
                           btnSize, btnSize);
}
