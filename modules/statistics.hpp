#ifndef SEGMENTER_STATISTICS_HPP_INCLUDED
#define SEGMENTER_STATISTICS_HPP_INCLUDED

#include <vector>

namespace Segmenter {

class Statistics
{
public:
    struct OutputFeatures {
        float entropyMean;
        float mfcc2Var;
        float mfcc3Var;
        float mfcc4Var;
        float deltaEntropyVar;
        float deltaMfcc2Var;
        float deltaMfcc3Var;
        float deltaMfcc4Var;
        float energyFlux;
    };

private:
    struct DeltaFeatures {
        float entropy;
        float mfcc2;
        float mfcc3;
        float mfcc4;
    };

    struct InputFeatures {
        float entropy;
        float mfcc2;
        float mfcc3;
        float mfcc4;
        float energy;
    };

    int m_windowSize;
    int m_stepSize;

    std::vector<float> m_deltaFilter;

    std::vector<InputFeatures> m_inputBuffer;
    std::vector<DeltaFeatures> m_deltaBuffer;
    std::vector<OutputFeatures> m_output;

    bool m_first;

public:
    Statistics( int windowSize = 132, int stepSize = 22, int deltaWindowSize = 5 ):
        m_windowSize(windowSize),
        m_stepSize(stepSize),
        m_output(0),
        m_first(false)
    {
        initDeltaFilter( deltaWindowSize );
    }

    void process ( float energy, const std::vector<float> & mfcc, float entropy, bool last )
    {
        m_output.clear();

        InputFeatures input;
        input.energy = energy;
        input.mfcc2 = mfcc[1];
        input.mfcc3 = mfcc[2];
        input.mfcc4 = mfcc[3];
        input.entropy = entropy;

        const int halfFilterLen = (m_deltaFilter.size() - 1) / 2;

        // populate input buffer;
        // pad beginning and end for the purpose of delta computations
        if (m_first) {
            m_first = false;
            m_inputBuffer.insert( m_inputBuffer.begin(),
                                    halfFilterLen,
                                    input );
        }

        m_inputBuffer.push_back(input);

        if (last) {
            m_inputBuffer.insert( m_inputBuffer.end(),
                                    halfFilterLen,
                                    input );
        }

        if (m_inputBuffer.size() < m_deltaFilter.size())
            return;

        // compute deltas for new inputs and populate delta buffer
        for (int idx = m_deltaBuffer.size(); idx <= m_inputBuffer.size() - m_deltaFilter.size(); ++idx)
        {
#define APPLY_FILTER( dst, member, srcIdx ) \
    dst.member = 0; \
    for (int filterIdx = 0; filterIdx < m_deltaFilter.size(); ++filterIdx) \
        dst.member += m_deltaFilter[filterIdx] * m_inputBuffer[srcIdx + filterIdx].member;

            DeltaFeatures delta;
            APPLY_FILTER( delta, entropy, idx );
            APPLY_FILTER( delta, mfcc2, idx );
            APPLY_FILTER( delta, mfcc3, idx );
            APPLY_FILTER( delta, mfcc4, idx );
            m_deltaBuffer.push_back( delta );

#undef APPLY_FILTER
        }

        // compute statistics on inputs & deltas
        int idx;
        for (idx = 0; idx <= (int) m_deltaBuffer.size() - m_windowSize; idx += m_stepSize)
        {
            //std::cout << "*********** idx = " << idx;
#define INPUT_VECTOR( feature ) \
        &m_inputBuffer[idx + halfFilterLen].feature, m_windowSize, (sizeof(InputFeatures) / sizeof(float))

#define DELTA_VECTOR( feature ) \
        &m_deltaBuffer[idx].feature, m_windowSize, (sizeof(DeltaFeatures) / sizeof(float))

            OutputFeatures output;
            output.entropyMean = mean( INPUT_VECTOR(entropy) );
            output.mfcc2Var = variance( INPUT_VECTOR(mfcc2 ) );
            output.mfcc3Var = variance( INPUT_VECTOR(mfcc3 ) );
            output.mfcc4Var = variance( INPUT_VECTOR(mfcc4 ) );
            output.deltaEntropyVar = variance( DELTA_VECTOR(entropy) );
            output.deltaMfcc2Var = variance( DELTA_VECTOR(entropy) );
            output.deltaMfcc3Var = variance( DELTA_VECTOR(entropy) );
            output.deltaMfcc4Var = variance( DELTA_VECTOR(entropy) );
            float energyMean = mean( INPUT_VECTOR(energy) );
            float energyVar = variance( INPUT_VECTOR(energy), energyMean );
            output.energyFlux = energyMean != 0.f ? energyVar / (energyMean * energyMean) : 0.f;

            m_output.push_back(output);

#undef INPUT_VECTOR
#undef DELTA_VECTOR
        }

        // remove processed inputs & deltas
        m_inputBuffer.erase( m_inputBuffer.begin(), m_inputBuffer.begin() + idx );
        m_deltaBuffer.erase( m_deltaBuffer.begin(), m_deltaBuffer.begin() + idx );
    }

    const std::vector<OutputFeatures> & output() const { return m_output; }

private:
    void initDeltaFilter( int filterLen )
    {
        int halfFilterLen = (filterLen - 1) / 2;
        m_deltaFilter.resize(2 * halfFilterLen + 1);
        float sum = 0;
        for (int i = -halfFilterLen; i <= halfFilterLen; ++i)
            sum += i*i;
        for (int i = -halfFilterLen; i <= halfFilterLen; ++i)
            m_deltaFilter[ halfFilterLen + i ] = i / sum;
    }

    void applyFilter( const std::vector<float> & filter, float *dst, float *src, size_t src_spacing )
    {
        int filterSize = filter.size();
        *dst = 0;
        for (int filterIdx = 0; filterIdx < filterSize; ++filterIdx) {
            *dst += *src * filter[filterIdx];
            src += src_spacing;
        }
    }

    float mean ( float *vec, int count, size_t spacing )
    {
        float mean = 0.f;
        for (int i = 0; i < count; ++i, vec += spacing)
            mean += *vec;
        mean /= count;
        return mean;
    }

    float variance ( float *vec, int count, size_t spacing )
    {
        float m = mean( vec, count, spacing );
        return variance( vec, count, spacing, m );
    }

    float variance ( float *vec, int count, size_t spacing, float mean )
    {
        float variance = 0.f;
        for (int i = 0; i < count; ++i, vec += spacing) {
            float dev = *vec - mean;
            variance += dev * dev;
        }
        variance /= count - 1;
        return variance;
    }
};

} // namespace SEGMENTER

#endif // SEGMENTER_STATISTICS_HPP_INCLUDED