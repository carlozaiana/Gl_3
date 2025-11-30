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
            currentOverviewMin = 10.0f; // Reset to high value
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

    // Threshold for switching to pre-calculated overview
    bool useOverview = (zoomX < 0.05f); 

    if (useOverview)
    {
        // === OVERVIEW MODE (Min/Max Candlesticks) ===
        // Solves "Lost Down Peaks" by drawing the range between Min and Max
        
        float pointSpacing = zoomX * (float)decimationFactor;
        
        for (int i = 0; i < overviewSize; ++i)
        {
            float x = w - ((float)i * pointSpacing);
            if (x < -10.0f) break;

            MinMax val = getSample(overviewBuffer, overviewWriteIndex, i);
            
            // Calculate vertical range
            // RMS is typically 0..1, so we draw up from midY
            float yMax = midY - (val.max * midY * 0.9f * zoomY);
            float yMin = midY - (val.min * midY * 0.9f * zoomY);
            
            yMax = juce::jlimit(0.0f, h, yMax);
            yMin = juce::jlimit(0.0f, h, yMin);
            
            // Add a vertical bar (Rectangle with width ~1px or line)
            // Using addRectangle is often cleaner for "bars" than drawing lines
            // Width is the spacing, but clamped to look nice (e.g. min 1px)
            float barWidth = std::max(1.0f, pointSpacing);
            
            // Note: yMax is technically "higher" visually but lower coordinate value than yMin
            path.addRectangle(x - barWidth*0.5f, yMax, barWidth, std::abs(yMin - yMax));
        }
    }
    else
    {
        // === RAW MODE (Optimized Pixel Grouping) ===
        // Solves "DAW Lag" by grouping samples into screen pixels
        
        int samplesToDraw = (int)std::ceil(w / zoomX) + 2;
        if (samplesToDraw > historySize) samplesToDraw = historySize;

        // Logic: We iterate Data, but we group by Screen Pixel.
        // We accumulate Min/Max for the current pixel column and draw 1 vertical line per pixel.
        
        int currentPixelX = -1000; // Sentinel
        float pixelMax = -1.0f;
        float pixelMin = 10.0f;

        for (int i = 0; i < samplesToDraw; ++i)
        {
            float rawX = w - ((float)i * zoomX);
            int px = (int)rawX; // Snap to integer pixel

            float val = getSample(historyBuffer, historyWriteIndex, i);

            if (px != currentPixelX)
            {
                // We moved to a new pixel column. Draw the previous one.
                if (currentPixelX > -1000 && pixelMax >= 0.0f)
                {
                    float yMax = midY - (pixelMax * midY * 0.9f * zoomY);
                    float yMin = midY - (pixelMin * midY * 0.9f * zoomY);
                    yMax = juce::jlimit(0.0f, h, yMax);
                    yMin = juce::jlimit(0.0f, h, yMin);
                    
                    // If zoomed very close, just draw a line connecting to next point?
                    // No, sticking to vertical bars is safer for preventing jumping peaks during scroll.
                    // But if zoomX is large (e.g. 10px per sample), bars look weird.
                    
                    if (zoomX >= 1.0f)
                    {
                        // Standard Line Graph if high resolution
                        // Note: This simple lineTo logic might miss "inter-sample" peaks if we had skipped, 
                        // but we are iterating every sample here, so it's fine.
                        if (path.isEmpty()) path.startNewSubPath(rawX + zoomX, yMax); // approximate prev
                        path.lineTo(rawX + zoomX, yMax); // This connects samples roughly
                    }
                    else
                    {
                        // High Density: Vertical Bar (Pixel Snapping)
                        // This is the Lag Fix.
                        // We draw a vertical line from min to max at this X.
                        path.startNewSubPath((float)currentPixelX, yMin);
                        path.lineTo((float)currentPixelX, yMax);
                    }
                }

                // Reset for new pixel
                currentPixelX = px;
                pixelMax = val;
                pixelMin = val;
            }
            else
            {
                // Accumulate within the same pixel
                if (val > pixelMax) pixelMax = val;
                if (val < pixelMin) pixelMin = val;
            }
        }
    }

    g.setColour (juce::Colours::cyan);
    
    // If we are drawing bars (overview or zoomed out raw), filling is often better/faster than stroking rects
    // But stroking a path of lines is also fine.
    if (useOverview || zoomX < 1.0f)
    {
        // High density / Overview look
        g.strokePath (path, juce::PathStrokeType (1.0f));
    }
    else
    {
        // Smooth line look for close up
        g.strokePath (path, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved));
    }

    // Stats
    g.setColour(juce::Colours::white);
    g.setFont(14.0f);
    juce::String mode = useOverview ? "Mode: OVERVIEW (MinMax)" : "Mode: RAW (Pixel Grouped)";
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