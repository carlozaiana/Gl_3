#include "PluginProcessor.h"
#include "PluginEditor.h"

SmoothScopeAudioProcessorEditor::SmoothScopeAudioProcessorEditor (SmoothScopeAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    // Initialize history buffer with 0
    historyBuffer.resize(historySize, 0.0f);

    setResizable(true, true);
    setResizeLimits(300, 200, 2000, 1000);
    setSize (800, 400);

    // 60 FPS refresh rate
    startTimerHz(60);
}

SmoothScopeAudioProcessorEditor::~SmoothScopeAudioProcessorEditor()
{
    stopTimer();
}

void SmoothScopeAudioProcessorEditor::timerCallback()
{
    // Pull data from Audio Processor's FIFO
    bool newData = false;
    
    while (true)
    {
        int currentRead = audioProcessor.fifoReadIndex.load(std::memory_order_acquire);
        int currentWrite = audioProcessor.fifoWriteIndex.load(std::memory_order_acquire);

        if (currentRead == currentWrite)
            break; 

        float val = audioProcessor.fifoBuffer[currentRead];

        int nextRead = (currentRead + 1) % SmoothScopeAudioProcessor::fifoSize;
        audioProcessor.fifoReadIndex.store(nextRead, std::memory_order_release);

        // Add to GUI History Circular Buffer
        historyBuffer[historyWriteIndex] = val;
        historyWriteIndex = (historyWriteIndex + 1) % historySize;
        
        newData = true;
    }

    if (newData)
        repaint();
}

// Helper: Get interpolated value from circular buffer (for smooth Zoom In)
float SmoothScopeAudioProcessorEditor::getInterpolatedValue(float index) const
{
    // index is "samples ago". 0 is most recent.
    // Map to actual buffer index.
    
    // Most recent sample is at (historyWriteIndex - 1)
    // 'index' moves backwards from there.
    
    float readPos = (float)(historyWriteIndex - 1) - index;
    
    // Handle wrapping for negative numbers manually to be safe with floats
    while (readPos < 0) readPos += (float)historySize;
    while (readPos >= (float)historySize) readPos -= (float)historySize;

    int idx0 = (int)readPos;
    int idx1 = (idx0 + 1) % historySize;
    float frac = readPos - (float)idx0;

    float v0 = historyBuffer[idx0];
    float v1 = historyBuffer[idx1];

    return v0 + frac * (v1 - v0);
}

// Helper: Get Max value in a range (for accurate Zoom Out/Peak detection)
float SmoothScopeAudioProcessorEditor::getMaxValueInRange(int startOffset, int endOffset) const
{
    // offsets are "samples ago"
    float maxVal = 0.0f;
    
    // Clamp range
    if (startOffset < 0) startOffset = 0;
    if (endOffset >= historySize) endOffset = historySize - 1;
    if (startOffset > endOffset) return 0.0f;

    // Iterate backwards relative to write head
    int currentIdx = historyWriteIndex - 1 - startOffset;
    // Fix initial wrap
    if (currentIdx < 0) currentIdx += historySize;

    int count = endOffset - startOffset + 1;
    
    for (int i = 0; i < count; ++i)
    {
        float v = historyBuffer[currentIdx];
        if (v > maxVal) maxVal = v;

        // Move back one
        currentIdx--;
        if (currentIdx < 0) currentIdx = historySize - 1;
    }
    
    return maxVal;
}

void SmoothScopeAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);

    auto area = getLocalBounds();
    float width = (float)area.getWidth();
    float height = (float)area.getHeight();
    float midY = height / 2.0f;

    // Grid
    g.setColour(juce::Colours::darkgrey.withAlpha(0.3f));
    g.drawHorizontalLine((int)midY, 0.0f, width);

    juce::Path path;
    
    // --- OPTIMIZED RENDERING ALGORITHM ---
    // Instead of looping through history samples, we loop through screen pixels.
    // This decouples rendering cost from zoom level.
    
    // Samples per horizontal pixel
    // If zoomX = 1.0, step = 1 sample per pixel.
    // If zoomX = 0.01, step = 100 samples per pixel.
    float samplesPerPixel = 1.0f / zoomX;
    
    // Start the path at the right edge (current time, offset 0)
    float firstVal = getInterpolatedValue(0.0f);
    float startY = midY - (firstVal * midY * 0.9f * zoomY);
    startY = juce::jlimit(0.0f, height, startY);
    path.startNewSubPath(width, startY);

    // Loop x from Right to Left
    // We use a step of 1 pixel (or larger for even higher performance, e.g. 2)
    for (float x = width - 1.0f; x >= 0.0f; x -= 1.0f)
    {
        // Calculate how many "samples ago" corresponds to this pixel X
        float distanceFromRight = width - x;
        
        // range in "samples ago" covered by this pixel
        float sampleIndexCenter = distanceFromRight * samplesPerPixel;
        
        float yVal = 0.0f;

        // STRATEGY SWITCHING:
        if (samplesPerPixel < 1.0f)
        {
            // ZOOMED IN: Linear Interpolation
            // When we have more pixels than samples, we need to interpolate 
            // to create a smooth line instead of steps.
            float val = getInterpolatedValue(sampleIndexCenter);
            yVal = midY - (val * midY * 0.9f * zoomY);
        }
        else
        {
            // ZOOMED OUT: Peak Detection
            // When one pixel covers many samples (e.g., 1000), we cannot just pick one
            // or we get aliasing (jitter). We must find the MAX in that range.
            
            int startRange = (int)((distanceFromRight - 1.0f) * samplesPerPixel); 
            int endRange   = (int)(distanceFromRight * samplesPerPixel);
            
            // Ensure we look at at least one sample
            if (endRange <= startRange) endRange = startRange + 1;

            float val = getMaxValueInRange(startRange, endRange);
            yVal = midY - (val * midY * 0.9f * zoomY);
        }

        yVal = juce::jlimit(0.0f, height, yVal);
        path.lineTo(x, yVal);
        
        // Optimization check: If we've exceeded our history buffer length, stop drawing
        if (sampleIndexCenter > (float)historySize) break;
    }

    g.setColour (juce::Colours::cyan);
    g.strokePath (path, juce::PathStrokeType (1.5f)); // Slightly thinner for cleaner look

    // Text Overlay
    g.setColour(juce::Colours::white);
    g.setFont(14.0f);
    g.drawText("Zoom X: " + juce::String(zoomX, 4) + " | Zoom Y: " + juce::String(zoomY, 2), 
               10, 10, 300, 20, juce::Justification::topLeft);
}

void SmoothScopeAudioProcessorEditor::mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    float scrollAmount = wheel.deltaY;
    
    if (event.mods.isCommandDown() || event.mods.isCtrlDown())
    {
        // Zoom Y (Amplitude)
        zoomY += (scrollAmount * 1.0f);
        zoomY = juce::jlimit(minZoomY, maxZoomY, zoomY);
    }
    else
    {
        // Zoom X (Time) - Logarithmic feel for better control
        // If scrolling up (positive), we multiply (zoom in). 
        // If scrolling down, we divide (zoom out).
        
        float scaleFactor = (scrollAmount > 0) ? 1.1f : 0.9f;
        
        // Apply stronger scaling if scrolling fast, or simply repeat
        // We can also just add, but multiplication feels more natural for wide ranges
        if (std::abs(scrollAmount) > 0.0f)
        {
            if (scrollAmount > 0) zoomX *= 1.1f;
            else                  zoomX *= 0.9f;
        }
        
        zoomX = juce::jlimit(minZoomX, maxZoomX, zoomX);
    }
    
    repaint();
}

void SmoothScopeAudioProcessorEditor::resized()
{
}