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
    // 1. HIGH RES BUFFER (Raw samples)
    // Size = 1,048,576 (2^20). Approx 3 hours at 60fps updates.
    static constexpr int historySize = 1048576; 
    std::vector<float> historyBuffer;
    int historyWriteIndex = 0;

    // 2. LOW RES OVERVIEW BUFFER (Static Mipmap)
    // Stores the Max of every 256 samples.
    // This guarantees stability when zoomed out.
    static constexpr int decimationFactor = 256;
    static constexpr int overviewSize = historySize / decimationFactor;
    std::vector<float> overviewBuffer;
    int overviewWriteIndex = 0;

    // Accumulator for generating the overview
    float currentBatchMax = 0.0f;
    int currentBatchCount = 0;

    // --- Zoom Parameters ---
    float zoomX = 1.0f;
    float zoomY = 1.0f;

    // Constraints
    const float minZoomX = 0.0001f; // Allows massive zoom out (entire buffer)
    const float maxZoomX = 50.0f;
    const float minZoomY = 0.5f;
    const float maxZoomY = 10.0f;
    
    // Helpers
    float getInterpolatedHistory(float samplesAgo) const;
    float getOverviewSample(int blocksAgo) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SmoothScopeAudioProcessorEditor)
};