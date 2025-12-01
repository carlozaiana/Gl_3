#include "PluginProcessor.h"
#include "PluginEditor.h"

SmoothScopeAudioProcessorEditor::SmoothScopeAudioProcessorEditor (SmoothScopeAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    historyBuffer.resize(historySize, 0.0f);
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

        // 2. Accumulate Overview
        if (val > currentOverviewMax) currentOverviewMax = val;
        if (val < currentOverviewMin) currentOverviewMin = val;
        currentOverviewCounter++;

        if (currentOverviewCounter >= decimationFactor)
        {
            overviewBuffer[overviewWriteIndex] = { currentOverviewMin, currentOverviewMax };
            overviewWriteIndex = (overviewWriteIndex + 1) % overviewSize;
            
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

    bool useOverview = (zoomX < 0.05f); 

    if (useOverview)
    {
        // === OVERVIEW MODE (Filled Envelope) ===
        // Used for extreme zoom out. Draws density.
        
        juce::Path roofPath;
        juce::Path floorPath;
        
        // Build vectors first to create a closed shape
        std::vector<juce::Point<float>> pointsMax;
        std::vector<juce::Point<float>> pointsMin;
        pointsMax.reserve((int)w);
        pointsMin.reserve((int)w);

        float pointSpacing = zoomX * (float)decimationFactor;
        
        for (int i = 0; i < overviewSize; ++i)
        {
            float x = w - ((float)i * pointSpacing);
            if (x < -10.0f) break;

            MinMax val = getSample(overviewBuffer, overviewWriteIndex, i);
            
            float yMax = midY - (val.max * midY * 0.9f * zoomY);
            float yMin = midY - (val.min * midY * 0.9f * zoomY);
            
            // Clamp
            yMax = juce::jlimit(0.0f, h, yMax);
            yMin = juce::jlimit(0.0f, h, yMin);
            
            pointsMax.emplace_back(x, yMax);
            pointsMin.emplace_back(x, yMin);
        }

        // Construct filled path
        if (!pointsMax.empty())
        {
            juce::Path fillPath;
            fillPath.startNewSubPath(pointsMax[0]);
            for (size_t i = 1; i < pointsMax.size(); ++i) fillPath.lineTo(pointsMax[i]);
            for (int i = (int)pointsMin.size() - 1; i >= 0; --i) fillPath.lineTo(pointsMin[i]);
            fillPath.closeSubPath();

            g.setColour(juce::Colours::cyan.withAlpha(0.5f));
            g.fillPath(fillPath);
            
            g.setColour(juce::Colours::cyan);
            g.strokePath(fillPath, juce::PathStrokeType(1.0f));
        }
    }
    else
    {
        // === RAW MODE (Optimized Line Graph) ===
        // Used for High Zoom AND Mid Zoom.
        // We discarded the "Min" value here to prevent Moiré interference.
        // We use Pixel Grouping (finding Max per pixel) to prevent lag.

        juce::Path path;
        bool started = false;

        int samplesToDraw = (int)std::ceil(w / zoomX) + 2;
        if (samplesToDraw > historySize) samplesToDraw = historySize;

        int currentPixelX = -1000;
        float pixelMax = -1.0f;

        for (int i = 0; i < samplesToDraw; ++i)
        {
            float rawX = w - ((float)i * zoomX);
            int px = (int)rawX;

            float val = getSample(historyBuffer, historyWriteIndex, i);

            if (px != currentPixelX)
            {
                // New pixel column reached. Draw the Max of the previous column.
                if (currentPixelX > -1000 && pixelMax >= 0.0f)
                {
                    float y = midY - (pixelMax * midY * 0.9f * zoomY);
                    y = juce::jlimit(0.0f, h, y);

                    if (!started) 
                    { 
                        path.startNewSubPath((float)currentPixelX, y); 
                        started = true; 
                    }
                    else 
                    { 
                        path.lineTo((float)currentPixelX, y); 
                    }
                }

                // Reset
                currentPixelX = px;
                pixelMax = val;
            }
            else
            {
                // Still in same pixel, keep finding the Max
                if (val > pixelMax) pixelMax = val;
            }
        }

        // Draw with a slightly thicker line to prevent aliasing "sparkles"
        // and to make the line feel solid like the overview block.
        g.setColour(juce::Colours::cyan);
        g.strokePath(path, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved));
    }

    // Stats
    g.setColour(juce::Colours::white);
    g.setFont(14.0f);
    juce::String mode = useOverview ? "Mode: OVERVIEW (Envelope)" : "Mode: RAW (Stable Line)";
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