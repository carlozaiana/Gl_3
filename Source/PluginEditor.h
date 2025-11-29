#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class SmoothScopeAudioProcessorEditor : public juce::AudioProcessorEditor,
                                        public juce::Timer
{
public:
    SmoothScopeAudioProcessorEditor (SmoothScopeAudioProcessor&);
    ~SmoothScopeAudioProcessorEditor() override;

    // --- Component Lifecycle ---
    void paint (juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;

    // --- Interaction ---
    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;

private:
    SmoothScopeAudioProcessor& audioProcessor;

    // --- Visualization Data ---
    // Increased size to approx 1 million samples. 
    // At ~10ms per block, this covers ~10,000 seconds (roughly 3 hours).
    // Power of 2 facilitates easy bitwise wrapping if we wanted, though modulo is used here.
    static constexpr int historySize = 1048576; 
    std::vector<float> historyBuffer;
    int historyWriteIndex = 0;

    // --- Zoom Parameters ---
    float zoomX = 1.0f; // Pixels per sample
    float zoomY = 1.0f;

    // Constants for constraints
    // 0.0005f allows squashing ~2000 samples into 1 pixel (huge zoom out)
    const float minZoomX = 0.0005f; 
    const float maxZoomX = 50.0f;
    const float minZoomY = 0.5f;
    const float maxZoomY = 10.0f;

    // Helper for linear interpolation
    float getInterpolatedValue(float index) const;
    // Helper for peak detection
    float getMaxValueInRange(int startOffset, int endOffset) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SmoothScopeAudioProcessorEditor)
};