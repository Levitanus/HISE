#include "time_stretcher.h"

#define USE_JUCE_FFT 1

#if JUCE_WINDOWS
#define USE_VDSP_COMPLEX_MUL 0
#else
#define USE_VDSP_COMPLEX_MUL 1
#endif

#include "signalsmith-stretch.h"

namespace hise {
using namespace juce;

time_stretcher::time_stretcher(bool enabled):
    pimpl(nullptr)
{
    setEnabled(enabled);
    
}

time_stretcher::~time_stretcher()
{
    delete pimpl;
}

bool time_stretcher::isEnabled() const
{
    return pimpl != nullptr;
}

void time_stretcher::setEnabled(bool shouldBeEnabled)
{
    if (isEnabled() != shouldBeEnabled)
    {
        ScopedLock sl(stretchLock);

        if (!shouldBeEnabled)
        {
            if(!pimpl)
                delete pimpl;

            pimpl = nullptr;
        }
        else
        {
            pimpl = new signalsmith::stretch::FloatStretcher();

            if (numChannels != 0 && sourceSampleRate != 0.0)
            {
                pimpl->configure(2, 4096, 512);
            }

            pimpl->reset();
        }
    }
}

void time_stretcher::reset()
{
    ScopedLock sl(stretchLock);
    pimpl->reset();
}

void time_stretcher::configure(int numChannels_, double sourceSampleRate_)
{
    if(numChannels != numChannels_ ||
       sourceSampleRate != sourceSampleRate_)
    {
        ScopedLock sl(stretchLock);
        
        numChannels = numChannels_;
        sourceSampleRate = sourceSampleRate_;
     
        if(pimpl != nullptr && numChannels > 0 && sourceSampleRate > 0)
        {
            pimpl->configure(numChannels, 4096, 512);
            pimpl->reset();
        }
    }
}

struct Helpers
{
    static bool isSilent(float** data, int numSamples)
    {
        if (numSamples == 0)
            return true;

        float* l = data[0];
        float* r = data[1];

        using SSEFloat = dsp::SIMDRegister<float>;

        auto alignedL = SSEFloat::getNextSIMDAlignedPtr(l);
        auto alignedR = SSEFloat::getNextSIMDAlignedPtr(r);

        jassert(alignedL == l);

        auto numUnaligned = alignedL - l;
        auto numAligned = numSamples - numUnaligned;

        static const auto gain90dB = Decibels::decibelsToGain(-60.0f);
        
        constexpr int sseSize = SSEFloat::SIMDRegisterSize / sizeof(float);

        while (numAligned >= sseSize)
        {
            auto l_ = SSEFloat::fromRawArray(alignedL);
            auto r_ = SSEFloat::fromRawArray(alignedR);

            auto sqL = SSEFloat::abs(l_);
            auto sqR = SSEFloat::abs(r_);

            auto max = SSEFloat::max(sqL, sqR).sum();

            if (max > gain90dB)
                return false;

            alignedL += sseSize;
            alignedR += sseSize;
            numAligned -= sseSize;
        }

        return true;
    }
};

double time_stretcher::skipLatency(float** inputs, double ratio)
{
    TRACE_EVENT("dsp", "skipLatency");

    ScopedLock sl(stretchLock);
    
    pimpl->reset();
    
    auto numBeforeOutput = roundToInt(pimpl->outputLatency() + pimpl->inputLatency() * ratio);
    
    float* thisInputs[2];
    float* outputs[2];
    
    thisInputs[0] = inputs[0];
    thisInputs[1] = inputs[1];
    
    double currentPos = 0.0;

    pimpl->setEnableOutput(false);
    
    while(numBeforeOutput > 0)
    {
	    const int numInputs = jmin(numBeforeOutput, 512);
        const int numOutputs = static_cast<double>(numInputs) / ratio;
        
        outputs[0] = static_cast<float*>(alloca(numOutputs * sizeof(float)));
        outputs[1] = static_cast<float*>(alloca(numOutputs * sizeof(float)));
        
        pimpl->process(thisInputs, numInputs, outputs, numOutputs);
        
        currentPos += numInputs;

        if (currentPos >= 1536)
            pimpl->setEnableOutput(true);

        thisInputs[0] = inputs[0] + static_cast<int>(currentPos);
        thisInputs[1] = inputs[1] + static_cast<int>(currentPos);

        numBeforeOutput -= numInputs;
    }
    
    return currentPos;
}

void time_stretcher::process(float** input, int numInput, float** originalOutputs, int numOutput)
{
    juce::ScopedTryLock sl(stretchLock);

    if(sl.isLocked())
    {
        float* outputs[2];

        outputs[0] = originalOutputs[0];
        outputs[1] = originalOutputs[1];

        if (playbackRatio != 1.0)
        {
            jassert(numOutput <= (resampledBuffer.getNumSamples()));

            outputs[0] = resampledBuffer.getWritePointer(0);
            outputs[1] = resampledBuffer.getWritePointer(1);
        }

        pimpl->process(input, numInput, outputs, numOutput);
        
        static constexpr float minus3dB = 0.5;//0.707106781186548f;
        
        for(int i = 0; i < numChannels; i++)
            FloatVectorOperations::multiply(outputs[i], minus3dB, numOutput);

        if (playbackRatio != 1.0)
        {
            for (int c = 0; c < numChannels; c++)
            {
                auto b = outputs[c];
                auto size = numOutput;

                double uptime = 0.0;

                auto ptr = originalOutputs[c];

                auto realNumOutputs = roundToInt((double)numOutput / playbackRatio);

                for (int i = 0; i < realNumOutputs; i++)
                {
                    auto loIndex = (int)uptime;
                    auto alpha = (float)uptime - (float)loIndex;
                    auto hiIndex = jmin(size - 1, loIndex + 1);

                    auto lo = b[loIndex];
                    auto hi = b[hiIndex];

                    auto v = Interpolator::interpolateLinear(b[loIndex], b[hiIndex], alpha);

                    ptr[i] = v;
                    uptime += playbackRatio;
                }
            }
        }
    }
}

void time_stretcher::setTransposeSemitones(double semiTones, double tonality)
{
    pimpl->setTransposeSemitones(semiTones, tonality);
}

void time_stretcher::setResampleBuffer(double ratio, float* resampleBuffer_, int size)
{
    if (ratio != playbackRatio)
    {
        playbackRatio = ratio;

        if (playbackRatio != 1.0)
        {
            float* s[2];

            s[0] = resampleBuffer_;
            s[1] = resampleBuffer_ + size/2;


            resampledBuffer = AudioSampleBuffer(s, 2, size/2);
        }
        else
        {
            resampledBuffer = {};
        }
    }
}
}