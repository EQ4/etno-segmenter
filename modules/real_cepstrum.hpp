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

#ifndef SEGMENTER_REAL_CEPSTRUM_INCLUDED
#define SEGMENTER_REAL_CEPSTRUM_INCLUDED

#include "module.hpp"

#include <vector>
#include <cmath>
#include <fftw3.h>
#include <cassert>

namespace Segmenter {

class RealCepstrum : public Module
{
    fftwf_plan m_plan;
    float *m_fft_in;
    float *m_fft_out;

    int m_bufSize;

    std::vector<float> m_output;
    float m_outputScale;

public:

    RealCepstrum( int windowSize )
    {
        assert( windowSize >= 2 );

        m_bufSize = (windowSize / 2) + 1;

        m_fft_in = fftwf_alloc_real(m_bufSize);
        m_fft_out = fftwf_alloc_real(m_bufSize);
        m_plan = fftwf_plan_r2r_1d(m_bufSize, m_fft_in, m_fft_out,
                                   FFTW_REDFT10, FFTW_ESTIMATE);

        for (int i = 0; i < m_bufSize; ++i)
            m_fft_in[i] = 0.f;

        m_output.resize(m_bufSize);

        m_outputScale = 1.f / std::sqrt( 2.0f * m_bufSize );
    }

    ~RealCepstrum()
    {
        fftwf_free(m_fft_in);
        fftwf_free(m_fft_out);
        fftwf_destroy_plan(m_plan);
    }

    void process ( const std::vector<float> & spectrumMagnitude )
    {
        static const float ath = 1.0f/65536;

        int nSpectrum = spectrumMagnitude.size();

        for (int i = 0; i < nSpectrum; ++i ) {
            float val = std::max( spectrumMagnitude[i], ath );
            // NOTE: Officially, the following should be log instead of sqrt.
            // sqrt reportedly proved better at classification.
            val = std::sqrt(val);
            m_fft_in[i] = val;
        }

        fftwf_execute( m_plan );

        m_fft_out[0] /= sqrt(2.0f);
        for (int i = 0; i < nSpectrum; ++i)
            //m_output[i] = m_fft_out[i] * m_outputScale * 0.5f;
            m_output[i] = m_fft_out[i] * 0.5f;
    }

    const std::vector<float> & output() const { return m_output; }
};

} // namespace Segmenter

#endif // SEGMENTER_REAL_CEPSTRUM_INCLUDED
