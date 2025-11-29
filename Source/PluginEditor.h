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

    // --- Visualization Data ---
    // 1. RAW HISTORY (High Detail)
    // 2^20 = ~1 Million samples. Approx 3-4 hours depending on block size/rate.
    static constexpr int historySize = 1048576; 
    std::vector<float> historyBuffer;
    int historyWriteIndex = 0;

    // 2. OVERVIEW (Low Detail / High Performance)
    // Stores the MAX value of every 'decimationFactor' samples.
    // This prevents us from having to loop over millions of samples when zoomed out.
    static constexpr int decimationFactor = 64;
    static constexpr int overviewSize = historySize / decimationFactor;
    std::vector<float> overviewBuffer;
    int overviewWriteIndex = 0;

    // Accumulators for generating the overview
    float currentBatchMax = 0.0f;
    int currentBatchCount = 0;

    // --- Zoom Parameters ---
    float zoomX = 1.0f;
    float zoomY = 1.0f;

    const float minZoomX = 0.0001f; 
    const float maxZoomX = 50.0f;
    const float minZoomY = 0.5f;
    const float maxZoomY = 10.0f;
    
    // --- Helpers ---
    // Finds the Maximum value in the circular buffer within a specific range.
    // Handles wrapping automatically.
    float getMaxInRange(const std::vector<float>& buffer, int writeIndex, int startOffset, int endOffset) const;
    
    // Simple linear interpolation for extreme close-ups
    float getInterpolatedValue(float sampleIndex) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SmoothScopeAudioProcessorEditor)
};