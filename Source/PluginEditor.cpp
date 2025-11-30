#include "PluginProcessor.h"
#include "PluginEditor.h"

SmoothScopeAudioProcessorEditor::SmoothScopeAudioProcessorEditor (SmoothScopeAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    historyBuffer.resize(historySize, 0.0f);
    overviewBuffer.resize(overviewSize, 0.0f);

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

        // 1. Store to Raw History
        historyBuffer[historyWriteIndex] = val;
        historyWriteIndex = (historyWriteIndex + 1) % historySize;

        // 2. Accumulate for Overview
        // We track the Max value to preserve peaks when zoomed out
        if (val > currentOverviewMax) currentOverviewMax = val;
        currentOverviewCounter++;

        if (currentOverviewCounter >= decimationFactor)
        {
            overviewBuffer[overviewWriteIndex] = currentOverviewMax;
            overviewWriteIndex = (overviewWriteIndex + 1) % overviewSize;
            
            // Reset
            currentOverviewMax = 0.0f;
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

    juce::Path path;
    bool pathStarted = false;

    // --- VISUALIZATION STRATEGY ---
    // To avoid jitter ("jumping peaks"), we must iterate the DATA, not the pixels.
    // We project the fixed data points onto the screen.
    
    // Threshold: When do we switch to the Overview buffer?
    // If points in the Raw buffer are closer than 0.5 pixels, it's wasted effort and messy.
    // We switch to Overview which has 64x fewer points.
    
    // zoomX is "pixels per raw sample".
    bool useOverview = (zoomX < 0.05f); // Heuristic threshold

    if (useOverview)
    {
        // === DRAW OVERVIEW BUFFER ===
        // Spacing between points in overview is 64x wider than raw
        float pointSpacing = zoomX * (float)decimationFactor;
        
        // Start from most recent (index 0 means "0 blocks ago")
        for (int i = 0; i < overviewSize; ++i)
        {
            // Calculate Screen X based on buffer index.
            // This maps the Data Grid (Stable) to Screen Grid.
            float x = w - ((float)i * pointSpacing);
            
            // Optimization: Stop if off screen
            if (x < -10.0f) break;

            float val = getSample(overviewBuffer, overviewWriteIndex, i);
            
            // Draw symmetrical volume around center
            float y = midY - (val * midY * 0.9f * zoomY);
            y = juce::jlimit(0.0f, h, y);

            if (!pathStarted) { path.startNewSubPath(x, y); pathStarted = true; }
            else              { path.lineTo(x, y); }
        }
    }
    else
    {
        // === DRAW RAW BUFFER ===
        // Iterate every sample (step = 1). Do not skip!
        // Skipping causes aliasing.
        
        // We can optimize the loop count though:
        // How many samples fit on screen? Width / zoomX
        int samplesToDraw = (int)std::ceil(w / zoomX) + 2;
        if (samplesToDraw > historySize) samplesToDraw = historySize;

        for (int i = 0; i < samplesToDraw; ++i)
        {
            float x = w - ((float)i * zoomX);
            
            float val = getSample(historyBuffer, historyWriteIndex, i);
            
            float y = midY - (val * midY * 0.9f * zoomY);
            y = juce::jlimit(0.0f, h, y);

            if (!pathStarted) { path.startNewSubPath(x, y); pathStarted = true; }
            else              { path.lineTo(x, y); }
        }
    }

    g.setColour (juce::Colours::cyan);
    // Use a slightly thinner line for overview to keep it sharp
    g.strokePath (path, juce::PathStrokeType (1.5f));

    // Info Overlay
    g.setColour(juce::Colours::white);
    g.setFont(14.0f);
    juce::String mode = useOverview ? "Mode: OVERVIEW (Stable)" : "Mode: RAW (Detail)";
    g.drawText(mode + " | Zoom: " + juce::String(zoomX, 5), 
               10, 10, 300, 20, juce::Justification::topLeft);
}

void SmoothScopeAudioProcessorEditor::mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    float scrollAmount = wheel.deltaY;
    
    if (event.mods.isCommandDown() || event.mods.isCtrlDown())
    {
        // Amplitude Zoom
        zoomY += (scrollAmount * 1.0f);
        zoomY = juce::jlimit(minZoomY, maxZoomY, zoomY);
    }
    else
    {
        // Time Zoom
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