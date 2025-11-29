#pragma once

#include <JuceHeader.h>

class SmoothScopeAudioProcessor : public juce::AudioProcessor
{
public:
    SmoothScopeAudioProcessor();
    ~SmoothScopeAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "SmoothScope"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int index) override {}
    const juce::String getProgramName (int index) override { return {}; }
    void changeProgramName (int index, const juce::String& newName) override {}

    void getStateInformation (juce::MemoryBlock& destData) override {}
    void setStateInformation (const void* data, int sizeInBytes) override {}

    // --- Data Exchange for Visualization ---
    // Simple thread-safe single-reader single-writer FIFO
    static constexpr int fifoSize = 1024;
    float fifoBuffer[fifoSize];
    std::atomic<int> fifoWriteIndex { 0 };
    std::atomic<int> fifoReadIndex { 0 };

    // Helper to write to FIFO
    void pushToFifo(float rmsValue)
    {
        int currentWrite = fifoWriteIndex.load(std::memory_order_acquire);
        int nextWrite = (currentWrite + 1) % fifoSize;

        if (nextWrite != fifoReadIndex.load(std::memory_order_acquire))
        {
            fifoBuffer[currentWrite] = rmsValue;
            fifoWriteIndex.store(nextWrite, std::memory_order_release);
        }
    }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SmoothScopeAudioProcessor)
};