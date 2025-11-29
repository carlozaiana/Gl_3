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
    
    // Drain FIFO
    while (true)
    {
        int currentRead = audioProcessor.fifoReadIndex.load(std::memory_order_acquire);
        int currentWrite = audioProcessor.fifoWriteIndex.load(std::memory_order_acquire);

        if (currentRead == currentWrite) break; 

        float val = audioProcessor.fifoBuffer[currentRead];

        int nextRead = (currentRead + 1) % SmoothScopeAudioProcessor::fifoSize;
        audioProcessor.fifoReadIndex.store(nextRead, std::memory_order_release);

        // 1. Update Raw Buffer
        historyBuffer[historyWriteIndex] = val;
        historyWriteIndex = (historyWriteIndex + 1) % historySize;

        // 2. Update Overview Buffer (Accumulate Max)
        if (val > currentBatchMax) currentBatchMax = val;
        currentBatchCount++;

        if (currentBatchCount >= decimationFactor)
        {
            overviewBuffer[overviewWriteIndex] = currentBatchMax;
            overviewWriteIndex = (overviewWriteIndex + 1) % overviewSize;
            currentBatchMax = 0.0f;
            currentBatchCount = 0;
        }
        
        newData = true;
    }

    if (newData)
        repaint();
}

// Optimized Range Max Finder
float SmoothScopeAudioProcessorEditor::getMaxInRange(const std::vector<float>& buffer, int writeIdx, int startAgo, int endAgo) const
{
    int bufSize = (int)buffer.size();
    
    // Clamp offsets
    if (startAgo < 0) startAgo = 0;
    if (endAgo >= bufSize) endAgo = bufSize - 1;
    if (startAgo > endAgo) return 0.0f;

    // Convert "samples ago" to actual buffer indices
    // "Ago" means we move backwards from writeIdx
    // startAgo is closer to NOW (higher index in circular buffer usually)
    // endAgo is further in history
    
    // We want to scan from (writeIdx - 1 - startAgo) down to (writeIdx - 1 - endAgo)
    
    int idxStart = writeIdx - 1 - startAgo; 
    int idxEnd   = writeIdx - 1 - endAgo;
    
    float maxVal = 0.0f;

    // Handle wrapping logic efficiently
    // Case 1: No wrap (range is continuous in array)
    // Case 2: Wrap (range splits across end/start of array)
    
    auto getVal = [&](int i) {
        while (i < 0) i += bufSize;
        while (i >= bufSize) i -= bufSize;
        return buffer[i];
    };

    // Since we are just finding Max, precise iteration order doesn't matter, just coverage.
    // But for performance, let's iterate linearly.
    int count = endAgo - startAgo + 1;
    int currentIdx = idxStart;
    
    // Basic implementation: Iterate backwards handling wrap
    for (int i = 0; i < count; ++i)
    {
        if (currentIdx < 0) currentIdx += bufSize;
        float v = buffer[currentIdx];
        if (v > maxVal) maxVal = v;
        currentIdx--;
    }

    return maxVal;
}

float SmoothScopeAudioProcessorEditor::getInterpolatedValue(float sampleIndex) const
{
    // sampleIndex is "samples ago"
    float readPos = (float)(historyWriteIndex - 1) - sampleIndex;
    while (readPos < 0) readPos += (float)historySize;
    while (readPos >= (float)historySize) readPos -= (float)historySize;

    int idx0 = (int)readPos;
    int idx1 = (idx0 + 1) % historySize;
    float frac = readPos - (float)idx0;

    float v0 = historyBuffer[idx0];
    float v1 = historyBuffer[idx1];
    return v0 + frac * (v1 - v0);
}

void SmoothScopeAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);

    auto area = getLocalBounds();
    int w = area.getWidth();
    float h = (float)area.getHeight();
    float midY = h / 2.0f;

    g.setColour(juce::Colours::darkgrey.withAlpha(0.5f));
    g.drawHorizontalLine((int)midY, 0.0f, (float)w);

    // Samples per pixel determines our strategy
    float samplesPerPixel = 1.0f / zoomX;

    juce::Path path;
    bool pathStarted = false;

    // STRATEGY SELECTION:
    // 1. EXTREME ZOOM IN (1 pixel < 1 sample): Use Linear Interpolation (Draw curves)
    // 2. NORMAL/ZOOM OUT (1 pixel >= 1 sample): Use Peak Detection (Draw Max in Range)
    
    bool useOverview = (samplesPerPixel > (float)decimationFactor);

    // We iterate PIXELS, not samples. This ensures constant framerate.
    // We iterate x from right (Width) to left (0).
    // x = Width represents "Now" (Offset 0).
    
    for (int x = w; x >= 0; --x)
    {
        float distanceFromRight = (float)(w - x);
        float yVal = midY;

        if (samplesPerPixel < 1.0f)
        {
            // --- SUPER ZOOMED IN (Interpolation) ---
            // Here jumping isn't an issue because we are drawing lines BETWEEN samples.
            float exactSampleIndex = distanceFromRight * samplesPerPixel;
            float val = getInterpolatedValue(exactSampleIndex);
            yVal = midY - (val * midY * 0.9f * zoomY);
        }
        else
        {
            // --- NORMAL / ZOOMED OUT (Peak Detection) ---
            // This is the fix for "Jumping Peaks".
            // We calculate the time range covered by this single pixel.
            
            float startSample = (distanceFromRight) * samplesPerPixel;
            float endSample   = (distanceFromRight + 1.0f) * samplesPerPixel;
            
            // Convert to integers for indices
            int iStart = (int)startSample;
            int iEnd   = (int)endSample; // Inclusive scan will handle iEnd - 1
            if (iEnd <= iStart) iEnd = iStart + 1;

            float val = 0.0f;

            if (useOverview)
            {
                // Scan the Overview Buffer (Low Res) for performance
                // Scale indices down
                int ovStart = iStart / decimationFactor;
                int ovEnd   = iEnd   / decimationFactor;
                val = getMaxInRange(overviewBuffer, overviewWriteIndex, ovStart, ovEnd);
            }
            else
            {
                // Scan the Raw Buffer (High Res) for accuracy
                val = getMaxInRange(historyBuffer, historyWriteIndex, iStart, iEnd);
            }
            
            yVal = midY - (val * midY * 0.9f * zoomY);
        }

        yVal = juce::jlimit(0.0f, h, yVal);

        if (!pathStarted)
        {
            path.startNewSubPath((float)x, yVal);
            pathStarted = true;
        }
        else
        {
            path.lineTo((float)x, yVal);
        }
        
        // Optimization: If we passed the end of the buffer data
        float totalSamplesCheck = distanceFromRight * samplesPerPixel;
        if (totalSamplesCheck > (float)historySize) break;
    }

    g.setColour (juce::Colours::cyan);
    g.strokePath (path, juce::PathStrokeType (1.0f));

    // Stats
    g.setColour(juce::Colours::white);
    g.setFont(14.0f);
    juce::String zoomTxt = "Zoom: " + juce::String(zoomX, 4) + "x";
    juce::String modeTxt = useOverview ? " (Overview)" : " (Raw)";
    if (samplesPerPixel < 1.0f) modeTxt = " (Interpolated)";
    
    g.drawText(zoomTxt + modeTxt, 10, 10, 200, 20, juce::Justification::topLeft);
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