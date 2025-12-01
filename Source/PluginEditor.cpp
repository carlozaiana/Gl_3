#include "PluginProcessor.h"
#include "PluginEditor.h"

SmoothScopeAudioProcessorEditor::SmoothScopeAudioProcessorEditor (SmoothScopeAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    historyBuffer.resize(historySize, 0.0f);
    
    // Initialize overview buffer
    overviewBuffer.resize(overviewSize, {0.0f, 0.0f});

    setResizable(true, true);
    setResizeLimits(300, 200, 2000, 1000);
    setSize (800, 400);

    startTimerHz(60);
}

SmoothScopeAudioProcessorEditor::~SmoothScopeAudioProcessorEditor()
{
    stopTimer();
}

void SmoothScopeAudioProcessorEditor::timerCallback()
{
    bool newData = false;
    
    while (true)
    {
        int currentRead = audioProcessor.fifoReadIndex.load(std::memory_order_acquire);
        int currentWrite = audioProcessor.fifoWriteIndex.load(std::memory_order_acquire);

        if (currentRead == currentWrite) break; 

        float val = audioProcessor.fifoBuffer[currentRead];

        int nextRead = (currentRead + 1) % SmoothScopeAudioProcessor::fifoSize;
        audioProcessor.fifoReadIndex.store(nextRead, std::memory_order_release);

        // 1. Update Raw History
        historyBuffer[historyWriteIndex] = val;
        historyWriteIndex = (historyWriteIndex + 1) % historySize;

        // 2. Accumulate Overview (Min/Max)
        if (val > currentOverviewMax) currentOverviewMax = val;
        if (val < currentOverviewMin) currentOverviewMin = val;
        currentOverviewCounter++;

        if (currentOverviewCounter >= decimationFactor)
        {
            overviewBuffer[overviewWriteIndex] = { currentOverviewMin, currentOverviewMax };
            overviewWriteIndex = (overviewWriteIndex + 1) % overviewSize;
            
            // Reset Accumulators
            currentOverviewMax = 0.0f;
            currentOverviewMin = 10.0f;
            currentOverviewCounter = 0;
        }
        
        newData = true;
    }

    if (newData)
        repaint();
}

void SmoothScopeAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);

    auto area = getLocalBounds();
    float w = (float)area.getWidth();
    float h = (float)area.getHeight();
    float midY = h / 2.0f;

    g.setColour(juce::Colours::darkgrey.withAlpha(0.5f));
    g.drawHorizontalLine((int)midY, 0.0f, w);

    // We will build two lists of points: one for the "Roof" (Max) and one for the "Floor" (Min).
    // Then we connect them to form a filled shape.
    std::vector<juce::Point<float>> roofPoints;
    std::vector<juce::Point<float>> floorPoints;
    
    // Reserve memory to prevent reallocations during drawing
    roofPoints.reserve((int)w + 2);
    floorPoints.reserve((int)w + 2);

    bool useOverview = (zoomX < 0.05f); 

    if (useOverview)
    {
        // === OVERVIEW MODE (Envelope) ===
        // We iterate the Low-Res buffer
        float pointSpacing = zoomX * (float)decimationFactor;
        
        for (int i = 0; i < overviewSize; ++i)
        {
            float x = w - ((float)i * pointSpacing);
            if (x < -10.0f) break;

            MinMax val = getSample(overviewBuffer, overviewWriteIndex, i);
            
            float yMax = midY - (val.max * midY * 0.9f * zoomY);
            float yMin = midY - (val.min * midY * 0.9f * zoomY);
            
            yMax = juce::jlimit(0.0f, h, yMax);
            yMin = juce::jlimit(0.0f, h, yMin);
            
            roofPoints.emplace_back(x, yMax);
            floorPoints.emplace_back(x, yMin);
        }
    }
    else
    {
        // === RAW MODE ===
        
        if (zoomX >= 1.0f)
        {
            // HIGH DETAIL ZOOM (Line Graph)
            // When pixels are spread apart, we just draw a simple line.
            // Filling doesn't make sense here as points are distinct.
            
            juce::Path linePath;
            bool started = false;
            
            int samplesToDraw = (int)std::ceil(w / zoomX) + 2;
            if (samplesToDraw > historySize) samplesToDraw = historySize;

            for (int i = 0; i < samplesToDraw; ++i)
            {
                float x = w - ((float)i * zoomX);
                float val = getSample(historyBuffer, historyWriteIndex, i);
                float y = midY - (val * midY * 0.9f * zoomY);
                y = juce::jlimit(0.0f, h, y);

                if (!started) { linePath.startNewSubPath(x, y); started = true; }
                else          { linePath.lineTo(x, y); }
            }
            
            g.setColour(juce::Colours::cyan);
            g.strokePath(linePath, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved));
            
            // Draw stats and exit early
            g.setColour(juce::Colours::white);
            g.setFont(14.0f);
            g.drawText("Mode: RAW (Line) | Zoom: " + juce::String(zoomX, 5), 
                       10, 10, 300, 20, juce::Justification::topLeft);
            return;
        }
        else
        {
            // MID ZOOM (Envelope Fill)
            // This fixes the "Flicker". 
            // We group samples by pixel, calculate Min/Max for that pixel, 
            // and store them to build a smooth filled shape later.
            
            int samplesToDraw = (int)std::ceil(w / zoomX) + 2;
            if (samplesToDraw > historySize) samplesToDraw = historySize;

            int currentPixelX = -1000;
            float pixelMax = -1.0f;
            float pixelMin = 10.0f;

            for (int i = 0; i < samplesToDraw; ++i)
            {
                float rawX = w - ((float)i * zoomX);
                int px = (int)rawX;

                float val = getSample(historyBuffer, historyWriteIndex, i);

                if (px != currentPixelX)
                {
                    // Pixel changed: Push the accumulated results of the PREVIOUS pixel
                    if (currentPixelX > -1000 && pixelMax >= 0.0f)
                    {
                        float yMax = midY - (pixelMax * midY * 0.9f * zoomY);
                        float yMin = midY - (pixelMin * midY * 0.9f * zoomY);
                        
                        // Clamp
                        yMax = juce::jlimit(0.0f, h, yMax);
                        yMin = juce::jlimit(0.0f, h, yMin);

                        // Add to our shape lists
                        float xPos = (float)currentPixelX;
                        roofPoints.emplace_back(xPos, yMax);
                        floorPoints.emplace_back(xPos, yMin);
                    }

                    // Reset for new pixel
                    currentPixelX = px;
                    pixelMax = val;
                    pixelMin = val;
                }
                else
                {
                    // Accumulate min/max within the same pixel column
                    if (val > pixelMax) pixelMax = val;
                    if (val < pixelMin) pixelMin = val;
                }
            }
        }
    }

    // === CONSTRUCT THE ENVELOPE SHAPE ===
    if (!roofPoints.empty())
    {
        juce::Path envelopePath;
        
        // 1. Trace the Roof (Left to Right, or Right to Left depending on loop order)
        // Our loops ran Right (screen width) to Left (0). So roofPoints[0] is at the Right.
        
        envelopePath.startNewSubPath(roofPoints[0]);
        for (size_t i = 1; i < roofPoints.size(); ++i)
            envelopePath.lineTo(roofPoints[i]);

        // 2. Connect to Floor
        // We want to draw the floor from Left (history) back to Right (now) to close the loop
        // The floorPoints vector is currently Right -> Left.
        // So we iterate it backwards.
        
        for (int i = (int)floorPoints.size() - 1; i >= 0; --i)
            envelopePath.lineTo(floorPoints[i]);

        // 3. Close the shape (connects last floor point back to first roof point)
        envelopePath.closeSubPath();

        // Draw Fill
        g.setColour(juce::Colours::cyan.withAlpha(0.6f)); // Slightly transparent fill
        g.fillPath(envelopePath);
        
        // Draw Outline (optional, adds definition)
        g.setColour(juce::Colours::cyan);
        g.strokePath(envelopePath, juce::PathStrokeType(1.0f));
    }

    // Stats
    g.setColour(juce::Colours::white);
    g.setFont(14.0f);
    juce::String mode = useOverview ? "Mode: OVERVIEW (Envelope)" : "Mode: RAW (Envelope)";
    g.drawText(mode + " | Zoom: " + juce::String(zoomX, 5), 
               10, 10, 300, 20, juce::Justification::topLeft);
}

void SmoothScopeAudioProcessorEditor::mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    float scrollAmount = wheel.deltaY;
    
    if (event.mods.isCommandDown() || event.mods.isCtrlDown())
    {
        zoomY += (scrollAmount * 1.0f);
        zoomY = juce::jlimit(minZoomY, maxZoomY, zoomY);
    }
    else
    {
        if (std::abs(scrollAmount) > 0.0f)
        {
            float factor = (scrollAmount > 0) ? 1.1f : 0.9f;
            zoomX *= factor;
        }
        zoomX = juce::jlimit(minZoomX, maxZoomX, zoomX);
    }
    
    repaint();
}

void SmoothScopeAudioProcessorEditor::resized() {}