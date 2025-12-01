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
        // For extreme zoom out, we keep the filled envelope because 
        // the density is high enough that "shivering" isn't visible, 
        // and the fill helps readability.
        
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
            
            yMax = juce::jlimit(0.0f, h, yMax);
            yMin = juce::jlimit(0.0f, h, yMin);
            
            pointsMax.emplace_back(x, yMax);
            pointsMin.emplace_back(x, yMin);
        }

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
        // === RAW MODE (Double-Wire Envelope) ===
        // We calculate BOTH Max (Roof) and Min (Floor) for each pixel.
        // This stabilizes the "downward peaks".
        // We draw them as TWO separate lines instead of a filled shape.
        // This eliminates the Moiré shivering.

        juce::Path roofPath;
        juce::Path floorPath;
        
        bool started = false;

        int samplesToDraw = (int)std::ceil(w / zoomX) + 2;
        if (samplesToDraw > historySize) samplesToDraw = historySize;

        int currentPixelX = -1000;
        float pixelMax = -1.0f;
        float pixelMin = 10.0f; // Start high

        for (int i = 0; i < samplesToDraw; ++i)
        {
            float rawX = w - ((float)i * zoomX);
            int px = (int)rawX;

            float val = getSample(historyBuffer, historyWriteIndex, i);

            if (px != currentPixelX)
            {
                // Pixel changed: Plot the accumulated Max AND Min
                if (currentPixelX > -1000 && pixelMax >= 0.0f)
                {
                    float yMax = midY - (pixelMax * midY * 0.9f * zoomY);
                    float yMin = midY - (pixelMin * midY * 0.9f * zoomY);

                    yMax = juce::jlimit(0.0f, h, yMax);
                    yMin = juce::jlimit(0.0f, h, yMin);

                    if (!started) 
                    { 
                        roofPath.startNewSubPath((float)currentPixelX, yMax); 
                        floorPath.startNewSubPath((float)currentPixelX, yMin);
                        started = true; 
                    }
                    else 
                    { 
                        roofPath.lineTo((float)currentPixelX, yMax); 
                        floorPath.lineTo((float)currentPixelX, yMin);
                    }
                }

                // Reset
                currentPixelX = px;
                pixelMax = val;
                pixelMin = val;
            }
            else
            {
                // Accumulate
                if (val > pixelMax) pixelMax = val;
                if (val < pixelMin) pixelMin = val;
            }
        }

        // Draw both paths
        g.setColour(juce::Colours::cyan);
        
        // Use a stroke of 1.5f for a clean look. 
        // If the lines overlap (Zoomed In), they form a single line.
        // If they diverge (Zoomed Out), they show the range.
        g.strokePath(roofPath, juce::PathStrokeType(1.5f));
        g.strokePath(floorPath, juce::PathStrokeType(1.5f));
    }

    // Stats
    g.setColour(juce::Colours::white);
    g.setFont(14.0f);
    juce::String mode = useOverview ? "Mode: OVERVIEW (Filled)" : "Mode: RAW (Double Wire)";
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