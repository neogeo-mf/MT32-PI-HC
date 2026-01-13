//
// matrixrain.cpp
//
// mt32-pi - A baremetal MIDI synthesizer for Raspberry Pi
// Matrix Rain visualization ported from GENaJam-Pi
//

#include "lcd/animations/matrixrain.h"
#include "lcd/drivers/ssd1306.h"
#include "utility.h"
#include <circle/timer.h>
#include <circle/util.h>
#include <cstring>

CMatrixRain::CMatrixRain()
	: m_bInitialized(false),
	  m_nLastUpdateTime(0)
{
	memset(m_RainDrops, 0, sizeof(m_RainDrops));
}

void CMatrixRain::Initialize()
{
	// Initialize all columns with staggered starting positions for full coverage
	for (u8 i = 0; i < Columns; ++i)
	{
		m_RainDrops[i].bActive = true;  // Start all columns active
		m_RainDrops[i].y = (float)((rand() % 72) - 40);  // Stagger across screen height
		m_RainDrops[i].speed = 0.3f + ((float)(rand() % 100)) / 120.0f;  // Varied speeds
		m_RainDrops[i].nLength = 3 + (rand() % (MaxTrailLength - 2));
		// Spread char_offsets widely so adjacent columns look different
		m_RainDrops[i].nCharOffset = (i * 7 + (rand() % 5)) % MatrixCharCount;
		m_RainDrops[i].nSpawnTime = CTimer::GetClockTicks();
		m_RainDrops[i].nBrightness = 0;
	}

	m_bInitialized = true;
	m_nLastUpdateTime = CTimer::GetClockTicks();
}

void CMatrixRain::HandleMidiNote(u8 nNote, u8 nVelocity)
{
	// Boost 1-3 random columns based on velocity
	BoostRandomColumn(nVelocity);

	if (nVelocity > 80)
		BoostRandomColumn(nVelocity);

	if (nVelocity > 110)
		BoostRandomColumn(nVelocity);
}

void CMatrixRain::SpawnRainDrop(u8 nColumn)
{
	if (nColumn >= Columns)
		return;

	m_RainDrops[nColumn].bActive = true;
	m_RainDrops[nColumn].y = -6.0f;  // Start just above screen
	m_RainDrops[nColumn].speed = 0.3f + ((float)(rand() % 100)) / 200.0f;  // 0.3 to 0.8 pixels per update
	m_RainDrops[nColumn].nLength = 3 + (rand() % (MaxTrailLength - 2));  // 3 to MaxTrailLength
	m_RainDrops[nColumn].nCharOffset = rand() % MatrixCharCount;
	m_RainDrops[nColumn].nSpawnTime = CTimer::GetClockTicks();
	m_RainDrops[nColumn].nBrightness = 0;
}

void CMatrixRain::BoostRandomColumn(u8 nVelocity)
{
	// Pick a random column to boost
	u8 nColumn = rand() % Columns;

	// Boost this drop - reset to top with faster speed
	m_RainDrops[nColumn].y = -6.0f;  // Reset to top
	m_RainDrops[nColumn].speed = 0.8f + (nVelocity / 150.0f);  // Faster when MIDI triggered
	m_RainDrops[nColumn].nLength = 4 + (nVelocity / 40);  // Velocity affects length
	if (m_RainDrops[nColumn].nLength > MaxTrailLength)
		m_RainDrops[nColumn].nLength = MaxTrailLength;
	m_RainDrops[nColumn].nCharOffset = rand() % MatrixCharCount;  // New random chars
	m_RainDrops[nColumn].nBrightness = nVelocity;  // Bright flash for MIDI notes
}

void CMatrixRain::Update(unsigned int nTicks)
{
	if (!m_bInitialized)
		Initialize();

	unsigned nNow = CTimer::GetClockTicks();
	unsigned nDeltaMs = Utility::TicksToMillis(nNow - m_nLastUpdateTime);

	// Rate limit updates (aim for ~30fps)
	if (nDeltaMs < 33)
		return;

	m_nLastUpdateTime = nNow;

	// Update each column
	for (u8 i = 0; i < Columns; ++i)
	{
		if (m_RainDrops[i].bActive)
		{
			// Move drop down
			m_RainDrops[i].y += m_RainDrops[i].speed;

			// Animate character offset - each column changes at different rates
			if ((rand() % 15) < (1 + (i % 4)))
			{
				m_RainDrops[i].nCharOffset = (m_RainDrops[i].nCharOffset + 1 + (i % 3)) % MatrixCharCount;
			}

			// Decay MIDI brightness boost
			if (m_RainDrops[i].nBrightness > 0)
			{
				m_RainDrops[i].nBrightness = (m_RainDrops[i].nBrightness > 15) ?
					m_RainDrops[i].nBrightness - 15 : 0;
			}

			// Check if drop has fallen off screen (with trail) - immediately respawn
			if (m_RainDrops[i].y > ScreenHeight + (m_RainDrops[i].nLength * 6))
			{
				// Respawn at top with new random properties for continuous rain
				m_RainDrops[i].y = (float)((rand() % 20) - 25);  // Start above screen at varying heights
				m_RainDrops[i].speed = 0.3f + ((float)(rand() % 100)) / 120.0f;  // Randomize speed
				m_RainDrops[i].nLength = 3 + (rand() % (MaxTrailLength - 2));
				m_RainDrops[i].nCharOffset = rand() % MatrixCharCount;  // Fresh random chars
				m_RainDrops[i].nBrightness = 0;
			}
		}
	}
}

void CMatrixRain::Draw(CSSD1306& OLED)
{
	for (u8 col = 0; col < Columns; ++col)
	{
		if (!m_RainDrops[col].bActive)
			continue;

		int x = col * 6;  // 6 pixels per character column
		int head_y = (int)m_RainDrops[col].y;

		// Draw the trail (oldest to newest, dim to bright)
		for (int t = m_RainDrops[col].nLength - 1; t >= 0; --t)
		{
			int char_y = head_y - (t * 6);  // 6 pixels per character height

			// Skip if off-screen
			if (char_y < -5 || char_y >= (int)ScreenHeight)
				continue;

			// Get character for this position
			u8 char_idx = (m_RainDrops[col].nCharOffset + t) % MatrixCharCount;
			char c = MatrixChars[char_idx];

			// Head of trail (t=0) is brightest
			if (t == 0)
			{
				// Lead character - always draw
				char str[2] = {c, '\0'};
				OLED.PrintSmall(str, x, char_y, false);

				// If MIDI-triggered, add extra brightness effect
				if (m_RainDrops[col].nBrightness > 50)
				{
					// Draw a small highlight
					OLED.SetPixel(x + 2, char_y + 3);
				}
			}
			else if (t == 1)
			{
				// Second character - draw full
				char str[2] = {c, '\0'};
				OLED.PrintSmall(str, x, char_y, false);
			}
			else if (t == 2)
			{
				// Third character - still visible but we'll draw it
				char str[2] = {c, '\0'};
				OLED.PrintSmall(str, x, char_y, false);
			}
			else
			{
				// Trailing characters - draw as dimmer (fewer pixels)
				// Just draw partial character or dots
				if (char_y >= 0 && char_y < 32)
				{
					OLED.SetPixel(x + 2, char_y + 1);
					OLED.SetPixel(x + 2, char_y + 5);
				}
			}
		}
	}
}
