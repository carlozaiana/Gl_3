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

    if (zoomX >= 1.0f)
    {
        // ============================================================
        // ZONE 1: HIGH ZOOM (ZoomX >= 1.0)
        // Strategy: Floating Point Interpolation.
        // NO PIXEL GROUPING. This restores the perfect smoothness.
        // ============================================================
        
        juce::Path path;
        bool started = false;

        int samplesToDraw = (int)std::ceil(w / zoomX) + 2;
        if (samplesToDraw > historySize) samplesToDraw = historySize;

        for (int i = 0; i < samplesToDraw; ++i)
        {
            // Use precise floating point X. 
            // This allows the curve to slide smoothly between pixels.
            float x = w - ((float)i * zoomX);
            
            float val = getSample(historyBuffer, historyWriteIndex, i);
            float y = midY - (val * midY * 0.9f * zoomY);
            y = juce::jlimit(0.0f, h, y);

            if (!started) { path.startNewSubPath(x, y); started = true; }
            else          { path.lineTo(x, y); }
        }
        
        g.setColour(juce::Colours::cyan);
        g.strokePath(path, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved));
    }
    else if (useOverview)
    {
        // ============================================================
        // ZONE 2: OVERVIEW / EXTREME ZOOM OUT (ZoomX < 0.05)
        // Strategy: Pre-Calculated MinMax Buffer.
        // ============================================================
        
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
        // ============================================================
        // ZONE 3: MID RANGE (0.05 <= ZoomX < 1.0)
        // Strategy: Pixel Grouping + Thickness Enforcement.
        // This fixes the "DAW Lag" AND the "Moiré Shivering".
        // ============================================================

        std::vector<juce::Point<float>> pointsMax;
        std::vector<juce::Point<float>> pointsMin;
        pointsMax.reserve((int)w);
        pointsMin.reserve((int)w);
        
        float samplesPerPixel = 1.0f / zoomX;

        // Iterate Screen Pixels (w down to 0)
        for (int x = (int)w; x >= 0; --x)
        {
            float distanceFromRight = (float)((int)w - x);
            
            // Calculate Range in Buffer
            float startSample = distanceFromRight * samplesPerPixel;
            float endSample   = (distanceFromRight + 1.0f) * samplesPerPixel;
            
            int iStart = (int)startSample;
            int iEnd   = (int)endSample;
            if (iEnd <= iStart) iEnd = iStart + 1;
            
            // Scan for Min/Max in Raw Buffer
            // We inline the scan here for clarity/speed in this specific loop
            float maxV = -1.0f;
            float minV = 10.0f;
            
            for (int i = iStart; i < iEnd; ++i)
            {
                // Check bounds
                if (i >= historySize) break; 

                float val = getSample(historyBuffer, historyWriteIndex, i);
                if (val > maxV) maxV = val;
                if (val < minV) minV = val;
            }

            // If we found data (valid range)
            if (maxV >= 0.0f)
            {
                float yMax = midY - (maxV * midY * 0.9f * zoomY);
                float yMin = midY - (minV * midY * 0.9f * zoomY);
                
                yMax = juce::jlimit(0.0f, h, yMax);
                yMin = juce::jlimit(0.0f, h, yMin);

                // --- THICKNESS ENFORCEMENT ---
                // This fixes the "Moiré Shivering".
                // If the tube is too thin (< 1.5px), widen it artificially.
                float height = std::abs(yMin - yMax);
                const float minThickness = 1.5f;

                if (height < minThickness)
                {
                    float center = (yMax + yMin) * 0.5f;
                    yMax = center - (minThickness * 0.5f);
                    yMin = center + (minThickness * 0.5f);
                }
                // -----------------------------

                pointsMax.emplace_back((float)x, yMax);
                pointsMin.emplace_back((float)x, yMin);
            }
        }

        if (!pointsMax.empty())
        {
            juce::Path fillPath;
            // Trace Roof (Right to Left)
            fillPath.startNewSubPath(pointsMax[0]);
            for (size_t i = 1; i < pointsMax.size(); ++i) fillPath.lineTo(pointsMax[i]);
            
            // Trace Floor (Left to Right - reverse order of collection)
            // Since we collected Right-to-Left (w to 0), pointsMin[0] is Right.
            // We want to draw from Left (last point) back to Right (first point) to close loop?
            // Actually, Roof went Right->Left.
            // To close polygon, Floor should go Left->Right.
            // pointsMin is Right->Left. So we iterate it backwards.
            for (int i = (int)pointsMin.size() - 1; i >= 0; --i) fillPath.lineTo(pointsMin[i]);
            
            fillPath.closeSubPath();

            // Draw solid
            g.setColour(juce::Colours::cyan.withAlpha(0.6f));
            g.fillPath(fillPath);
            
            // Optional: lighter stroke on edges for definition
            g.setColour(juce::Colours::cyan);
            g.strokePath(fillPath, juce::PathStrokeType(1.0f));
        }
    }

    // Stats
    g.setColour(juce::Colours::white);
    g.setFont(14.0f);
    juce::String mode;
    if (zoomX >= 1.0f) mode = "Mode: RAW (Float)";
    else if (useOverview) mode = "Mode: OVERVIEW (MinMax)";
    else mode = "Mode: MID (Enforced Envelope)";
    
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