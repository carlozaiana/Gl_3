#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

// Simple struct to hold both peak and valley for a time range
struct MinMax
{
    float min;
    float max;
};

class SmoothScopeAudioProcessorEditor : public juce::AudioProcessorEditor,
                                        public juce::Timer
{
public:
    SmoothScopeAudioProcessorEditor (SmoothScopeAudioProcessor&);
    ~SmoothScopeAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;
    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;

private:
    SmoothScopeAudioProcessor& audioProcessor;

    // --- 1. RAW BUFFER (High Detail) ---
    // 1 Million samples ~ 3 hours.
    static constexpr int historySize = 1048576; 
    std::vector<float> historyBuffer;
    int historyWriteIndex = 0;

    // --- 2. OVERVIEW BUFFER (Min/Max) ---
    // Stores Min/Max pairs to preserve down-peaks (silence) when zoomed out.
    static constexpr int decimationFactor = 64;
    static constexpr int overviewSize = historySize / decimationFactor;
    std::vector<MinMax> overviewBuffer;
    int overviewWriteIndex = 0;

    // Accumulators for generating the overview
    float currentOverviewMax = 0.0f;
    float currentOverviewMin = 10.0f; // Start high so first sample overwrites it
    int currentOverviewCounter = 0;

    // --- Zoom Parameters ---
    float zoomX = 5.0f;
    float zoomY = 1.0f;

    const float minZoomX = 0.0001f;
    const float maxZoomX = 50.0f;
    const float minZoomY = 0.5f;
    const float maxZoomY = 10.0f;
    
    // Helper to get safe index from circular buffer
    template <typename T>
    T getSample(const std::vector<T>& buffer, int writeIndex, int samplesAgo) const
    {
        int idx = writeIndex - 1 - samplesAgo;
        int size = (int)buffer.size();
        while (idx < 0) idx += size;
        while (idx >= size) idx -= size;
        return buffer[idx];
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SmoothScopeAudioProcessorEditor)
};