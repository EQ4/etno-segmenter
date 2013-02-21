/*
    Etno Segmenter - automatic segmentation of etnomusicological recordings

    Copyright (c) 2012 - 2013 Matija Marolt & Jakob Leben

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software Foundation,
    Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
*/

#include "pipeline.hpp"

#include "resampler.hpp"
#include "energy.hpp"
#include "spectrum.hpp"
#include "mel_spectrum.hpp"
#include "real_cepstrum.hpp"
#include "mfcc.hpp"
#include "entropy.hpp"
#include "cepstral_features.hpp"
#include "4hz_modulation.hpp"
#include "statistics.hpp"
#include "classification.hpp"

namespace Segmenter {

Pipeline::Pipeline ( const InputContext & inCtx,
                     const FourierContext & fCtx,
                     const StatisticContext & statCtx):
    m_inputContext( inCtx ),
    m_fourierContext( fCtx ),
    m_statContext( statCtx ),
    m_resample( inCtx.sampleRate != fCtx.sampleRate )
{
    InputContext & in = m_inputContext;
    FourierContext & fourier = m_fourierContext;
    StatisticContext & stat = m_statContext;

    m_statsStepDuration = Vamp::RealTime::fromSeconds
        ( (double) stat.stepSize * m_fourierContext.stepSize / m_fourierContext.sampleRate );
    m_statsTime = Vamp::RealTime::fromSeconds
        ( ((double) stat.blockSize / 2.0) * m_fourierContext.stepSize / m_fourierContext.sampleRate );

    const int mfccFilterCount = 27;

    const int chromEntropyLoFreq = 55;
    const int chromEntropyHiFreq = 2000;

    const int statDeltaBlockSize = 5;

    m_modules.resize( ModuleCount );

    get(ResamplerModule) = new Segmenter::Resampler( in.sampleRate, fourier.sampleRate, inCtx.resampleType );
    get(EnergyModule) = new Segmenter::Energy( fourier.blockSize );
    get(PowerSpectrumModule) = new Segmenter::PowerSpectrum( fourier.blockSize );
    get(MelSpectrumModule) = new Segmenter::MelSpectrum( mfccFilterCount, fourier.sampleRate,  fourier.blockSize );
    get(MfccModule) = new Segmenter::Mfcc( mfccFilterCount );
    get(ChromaticEntropyModule) = new Segmenter::ChromaticEntropy( fourier.sampleRate, fourier.blockSize,
                                                          chromEntropyLoFreq, chromEntropyHiFreq );
    get(RealCepstrumModule) = new Segmenter::RealCepstrum( fourier.blockSize );
    get(CepstralFeaturesModule) = new Segmenter::CepstralFeatures( fourier.sampleRate, fourier.blockSize );
    get(FourHzModulationModule) = new Segmenter::FourHzModulation( fourier.sampleRate, fourier.blockSize, fourier.stepSize );
    get(StatisticsModule) = new Segmenter::Statistics(stat.blockSize, stat.stepSize, statDeltaBlockSize);
    get(ClassifierModule) = new Segmenter::Classifier();
}

Pipeline::~Pipeline()
{
    int moduleCount = m_modules.size();

    for (int idx = 0; idx < m_modules.size(); ++idx)
        delete m_modules[idx];
}

void Pipeline::computeStatistics( const float * input, int inputSize, bool endOfStream )
{
    Segmenter::Resampler *resampler = static_cast<Segmenter::Resampler*>( get(ResamplerModule) );
    Segmenter::Energy *energy = static_cast<Segmenter::Energy*>( get(EnergyModule) );
    Segmenter::PowerSpectrum *powerSpectrum = static_cast<Segmenter::PowerSpectrum*>( get(PowerSpectrumModule) );
    Segmenter::MelSpectrum *melSpectrum = static_cast<Segmenter::MelSpectrum*>( get(MelSpectrumModule) );
    Segmenter::Mfcc *mfcc = static_cast<Segmenter::Mfcc*>( get(MfccModule) );
    Segmenter::ChromaticEntropy *chromaticEntropy = static_cast<Segmenter::ChromaticEntropy*>( get(ChromaticEntropyModule) );
    Segmenter::Statistics *statistics = static_cast<Segmenter::Statistics*>( get(StatisticsModule) );
    Segmenter::FourHzModulation *fourHzMod = static_cast<Segmenter::FourHzModulation*>( get(FourHzModulationModule) );
    Segmenter::RealCepstrum *realCepstrum = static_cast<Segmenter::RealCepstrum*>( get(RealCepstrumModule) );
    Segmenter::CepstralFeatures *cepstralFeatures = static_cast<Segmenter::CepstralFeatures*>( get(CepstralFeaturesModule) );

    Vamp::Plugin::FeatureSet features;

    if (m_resample) {
        if (inputSize)
            resampler->process( input, inputSize, m_resampBuffer );
        if (endOfStream)
            resampler->processRemainingData( m_resampBuffer );
    }
    else {
        if (inputSize)
            m_resampBuffer.insert( m_resampBuffer.end(), input, input + inputSize );
    }

    m_featBuffer.clear();
    m_statsBuffer.clear();

    int blockFrame;
    int frameLimit = (int) m_resampBuffer.size() - m_fourierContext.blockSize;

    for ( blockFrame = 0;
          blockFrame <= frameLimit;
          blockFrame += m_fourierContext.stepSize )
    {
        const float *block = m_resampBuffer.data() + blockFrame;

        energy->process( block );

        powerSpectrum->process( block );

        const std::vector<float> & powerSpectrumOut = powerSpectrum->output();
        int nSpectrum = powerSpectrumOut.size();
        m_spectrumMag.resize( nSpectrum );
        for (int i = 0; i < nSpectrum; ++i)
            m_spectrumMag[i] = std::sqrt( powerSpectrumOut[i] );

        melSpectrum->process( m_spectrumMag );

        mfcc->process( melSpectrum->output() );

        chromaticEntropy->process( powerSpectrum->output() );

        fourHzMod->process( melSpectrum->output() );

        realCepstrum->process( m_spectrumMag );

        cepstralFeatures->process( m_spectrumMag, realCepstrum->output() );

        Statistics::InputFeatures statInput;
        statInput.energy = energy->output();
        statInput.entropy = chromaticEntropy->output();
        statInput.mfcc2 = mfcc->output()[2];
        statInput.mfcc3 = mfcc->output()[3];
        statInput.mfcc4 = mfcc->output()[4];
        statInput.pitchDensity = cepstralFeatures->pitchDensity();
        statInput.tonality = cepstralFeatures->tonality();
        statInput.tonality1 = cepstralFeatures->tonality1();
        statInput.fourHzMod = fourHzMod->output();

        m_featBuffer.push_back( statInput );

        statistics->process( statInput, m_statsBuffer );
    }

    if (endOfStream)
        statistics->processRemainingData( m_statsBuffer );

    m_resampBuffer.erase( m_resampBuffer.begin(), m_resampBuffer.begin() + blockFrame );
}

void Pipeline::computeClassification( Vamp::Plugin::FeatureList & output )
{
    Segmenter::Classifier *classifier = static_cast<Segmenter::Classifier*>( get(ClassifierModule) );

    for (int i = 0; i < m_statsBuffer.size(); ++i)
    {
        const Statistics::OutputFeatures & stat = m_statsBuffer[i];

        classifier->process( stat.features );

        const std::vector<float> & distribution = classifier->probabilities();
//#if 0
        float avgClass = 0;
        for (int i = 0; i < distribution.size(); ++i)
            avgClass += distribution[i] * i;
        avgClass /= distribution.size() - 1;
//#endif
        Vamp::Plugin::Feature classification;
        classification.hasTimestamp = true;
        classification.timestamp = m_statsTime;
        classification.values.push_back( avgClass );
        //classification.values = distribution;

        //classification.values.push_back( stat[ Statistics::TONALITY1_MEAN ] );

        //classification.values.resize( Statistics::OUTPUT_FEATURE_COUNT );
        //std::memcpy( classification.values.data(), &stat.features[0], Statistics::OUTPUT_FEATURE_COUNT * sizeof(float) );

        output.push_back( classification );

        m_statsTime = m_statsTime + m_statsStepDuration;
    }
}

}
