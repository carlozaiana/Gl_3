#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

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

    // --- 1. RAW BUFFER (Detailed) ---
    // Stores the incoming RMS blocks directly.
    // 1,048,576 samples covers ~3 hours at typical buffer rates (60fps logic).
    static constexpr int historySize = 1048576; 
    std::vector<float> historyBuffer;
    int historyWriteIndex = 0;

    // --- 2. OVERVIEW BUFFER (Stable for Zoom Out) ---
    // Stores the MAX value of every 'decimationFactor' samples.
    // 1048576 / 64 = ~16,384 points. Drawing 16k points is instant for the GPU.
    static constexpr int decimationFactor = 64;
    static constexpr int overviewSize = historySize / decimationFactor;
    std::vector<float> overviewBuffer;
    int overviewWriteIndex = 0;

    // Accumulator to calculate the max for the overview
    float currentOverviewMax = 0.0f;
    int currentOverviewCounter = 0;

    // --- Zoom Parameters ---
    float zoomX = 5.0f; // Default zoom: 5 pixels per sample (clearly visible)
    float zoomY = 1.0f;

    // Constraints
    const float minZoomX = 0.0001f; // Allows full 3 hours on screen
    const float maxZoomX = 50.0f;
    const float minZoomY = 0.5f;
    const float maxZoomY = 10.0f;
    
    // Helper to get safe index from circular buffer
    float getSample(const std::vector<float>& buffer, int writeIndex, int samplesAgo) const
    {
        int idx = writeIndex - 1 - samplesAgo;
        int size = (int)buffer.size();
        // Fast wrap
        while (idx < 0) idx += size;
        while (idx >= size) idx -= size; // Just in case
        return buffer[idx];
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SmoothScopeAudioProcessorEditor)
};