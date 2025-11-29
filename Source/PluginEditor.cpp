#include "PluginProcessor.h"
#include "PluginEditor.h"

SmoothScopeAudioProcessorEditor::SmoothScopeAudioProcessorEditor (SmoothScopeAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    // Initialize buffers
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

        // 1. Write to High Res History
        historyBuffer[historyWriteIndex] = val;
        historyWriteIndex = (historyWriteIndex + 1) % historySize;

        // 2. Accumulate for Low Res Overview
        if (val > currentBatchMax) currentBatchMax = val;
        currentBatchCount++;

        if (currentBatchCount >= decimationFactor)
        {
            overviewBuffer[overviewWriteIndex] = currentBatchMax;
            overviewWriteIndex = (overviewWriteIndex + 1) % overviewSize;
            
            // Reset accumulator
            currentBatchMax = 0.0f;
            currentBatchCount = 0;
        }
        
        newData = true;
    }

    if (newData)
        repaint();
}

// Helper: Linear Interpolation for Smooth Zoom In
float SmoothScopeAudioProcessorEditor::getInterpolatedHistory(float samplesAgo) const
{
    float readPos = (float)(historyWriteIndex - 1) - samplesAgo;
    while (readPos < 0) readPos += (float)historySize;
    while (readPos >= (float)historySize) readPos -= (float)historySize;

    int idx0 = (int)readPos;
    int idx1 = (idx0 + 1) % historySize;
    float frac = readPos - (float)idx0;

    return historyBuffer[idx0] + frac * (historyBuffer[idx1] - historyBuffer[idx0]);
}

// Helper: Direct lookup for Overview
float SmoothScopeAudioProcessorEditor::getOverviewSample(int blocksAgo) const
{
    int idx = overviewWriteIndex - 1 - blocksAgo;
    while (idx < 0) idx += overviewSize;
    return overviewBuffer[idx];
}

void SmoothScopeAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);

    auto area = getLocalBounds();
    float width = (float)area.getWidth();
    float height = (float)area.getHeight();
    float midY = height / 2.0f;

    g.setColour(juce::Colours::darkgrey.withAlpha(0.3f));
    g.drawHorizontalLine((int)midY, 0.0f, width);

    juce::Path path;
    
    // Logic: 
    // If zoomX is small (zoomed out), using raw history is slow and jittery.
    // We switch to 'overviewBuffer' if 1 pixel represents roughly 'decimationFactor' samples or more.
    
    // Threshold: If we are compressing more than 128 samples into a pixel, switch to Overview.
    // (decimationFactor is 256).
    bool useOverview = (zoomX < (1.0f / 64.0f)); 

    if (useOverview)
    {
        // --- RENDER MODE: OVERVIEW (Stable, Zoomed Out) ---
        
        // Effective zoom for the overview buffer
        // Since overview is 256x smaller, the pixels-per-point is 256x larger.
        float overviewZoom = zoomX * (float)decimationFactor;
        
        // Start drawing from the most recent block
        float firstVal = getOverviewSample(0);
        float startY = midY - (firstVal * midY * 0.9f * zoomY);
        startY = juce::jlimit(0.0f, height, startY);
        path.startNewSubPath(width, startY);

        // Iterate through the COMPRESSED buffer
        // Even for 3 hours of data (1M samples), overview is only ~4000 points.
        // We can loop through ALL necessary points without skipping.
        
        for (int i = 1; i < overviewSize; ++i)
        {
            float xPos = width - ((float)i * overviewZoom);
            
            if (xPos < -50.0f) break; // Optimization

            float val = getOverviewSample(i);
            float y = midY - (val * midY * 0.9f * zoomY);
            y = juce::jlimit(0.0f, height, y);

            path.lineTo(xPos, y);
        }
    }
    else
    {
        // --- RENDER MODE: RAW HISTORY (Smooth, Zoomed In) ---
        
        float firstVal = getInterpolatedHistory(0.0f);
        float startY = midY - (firstVal * midY * 0.9f * zoomY);
        startY = juce::jlimit(0.0f, height, startY);
        path.startNewSubPath(width, startY);

        // For performance when Zoomed In, we iterate PIXELS (0 to width)
        // and interpolate the audio value at that pixel.
        for (float x = width - 2.0f; x > -2.0f; x -= 2.0f) // Step 2px for speed
        {
            float samplesAgo = (width - x) / zoomX;
            
            if (samplesAgo >= historySize) break;

            float val = getInterpolatedHistory(samplesAgo);
            float y = midY - (val * midY * 0.9f * zoomY);
            y = juce::jlimit(0.0f, height, y);

            path.lineTo(x, y);
        }
    }

    g.setColour (juce::Colours::cyan);
    g.strokePath (path, juce::PathStrokeType (useOverview ? 1.0f : 2.0f));

    // Info
    g.setColour(juce::Colours::white);
    g.setFont(14.0f);
    juce::String mode = useOverview ? "Mode: OVERVIEW (Stable)" : "Mode: RAW (Detail)";
    g.drawText(mode + " | Zoom X: " + juce::String(zoomX, 5), 
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
        // Multiplicative zoom for vast ranges
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