#include "PluginProcessor.h"
#include "PluginEditor.h"

SmoothScopeAudioProcessor::SmoothScopeAudioProcessor()
     : AudioProcessor (BusesProperties().withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
}

SmoothScopeAudioProcessor::~SmoothScopeAudioProcessor() {}

void SmoothScopeAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock) {}

void SmoothScopeAudioProcessor::releaseResources() {}

void SmoothScopeAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    
    // Note: RMS is smooth, but if you want to catch sudden peaks (transients) 
    // better, buffer.getMagnitude(0, numSamples) is often preferred for scopes.
    // However, sticking to your RMS logic as requested:
    float rms = buffer.getRMSLevel(0, 0, buffer.getNumSamples());
    
    if (getTotalNumInputChannels() > 1)
    {
        float rightRms = buffer.getRMSLevel(1, 0, buffer.getNumSamples());
        rms = (rms + rightRms) * 0.5f;
    }

    pushToFifo(rms);
}

juce::AudioProcessorEditor* SmoothScopeAudioProcessor::createEditor()
{
    return new SmoothScopeAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SmoothScopeAudioProcessor();
}