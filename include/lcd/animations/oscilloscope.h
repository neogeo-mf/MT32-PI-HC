//
// oscilloscope.h
//
// mt32-pi - A baremetal MIDI synthesizer for Raspberry Pi
// Copyright (C) 2020-2023 Dale Whinham <daleyo@gmail.com>
//
// This file is part of mt32-pi.
//
// mt32-pi is free software: you can redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the Free Software
// Foundation, either version 3 of the License, or (at your option) any later
// version.
//
// mt32-pi is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
// FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
// details.
//
// You should have received a copy of the GNU General Public License along with
// mt32-pi. If not, see <http://www.gnu.org/licenses/>.
//

#ifndef _oscilloscope_h
#define _oscilloscope_h

#include <circle/types.h>

class CSSD1306;

// Oscilloscope visualization - MIDI-reactive waveform display
class COscilloscope
{
public:
	COscilloscope();

	void Initialize();
	void Update(unsigned int nTicks);
	void Draw(CSSD1306& OLED);
	void HandleMidiNote(u8 nNote, u8 nVelocity, u8 nChannel);

private:
	static constexpr u8 Channels = 6;
	static constexpr float ScreenWidth = 128.0f;
	static constexpr float ScreenHeight = 32.0f;
	static constexpr float CenterY = 16.0f;

	enum TWaveform
	{
		Sine = 0,
		Square,
		Saw,
		Triangle,
		Noise,
		Count
	};

	struct OscChannel
	{
		u8 nWaveform;        // Current waveform type
		float fFrequency;    // Wave frequency (affects how many cycles shown)
		float fAmplitude;    // Wave amplitude (0.0 - 1.0)
		float fPhase;        // Phase offset for animation
		u8 nDecay;           // Amplitude decay counter
		bool bActive;        // Currently triggered
	};

	OscChannel m_Channels[Channels];
	bool m_bInitialized;
	unsigned m_nLastUpdateTime;
	float m_fGlobalPhase;

	float GetWaveValue(u8 nWaveform, float fPos);
};

#endif
