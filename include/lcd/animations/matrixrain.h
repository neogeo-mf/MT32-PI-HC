//
// matrixrain.h
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

#ifndef _matrixrain_h
#define _matrixrain_h

#include <circle/types.h>

class CSSD1306;

// Matrix Rain visualization - MIDI-reactive falling characters
class CMatrixRain
{
public:
	CMatrixRain();

	void Initialize();
	void Update(unsigned int nTicks);
	void Draw(CSSD1306& OLED);
	void HandleMidiNote(u8 nNote, u8 nVelocity);

private:
	static constexpr u8 Columns = 21;  // 128 pixels / 6 pixels per char
	static constexpr u8 MaxTrailLength = 6;
	static constexpr float ScreenWidth = 128.0f;
	static constexpr float ScreenHeight = 32.0f;

	struct RainDrop
	{
		float y;              // Current Y position
		float speed;          // Fall speed (pixels per frame)
		u8 nLength;           // Trail length (3-8 characters)
		u8 nCharOffset;       // Character animation offset
		bool bActive;         // Currently falling
		unsigned nSpawnTime;
		u8 nBrightness;       // For MIDI-triggered brightness boost
	};

	RainDrop m_RainDrops[Columns];
	bool m_bInitialized;
	unsigned m_nLastUpdateTime;

	static constexpr const char* MatrixChars = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ@#$%&*<>[]{}";
	static constexpr u8 MatrixCharCount = 52;

	void SpawnRainDrop(u8 nColumn);
	void BoostRandomColumn(u8 nVelocity);
};

#endif
