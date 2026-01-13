//
// oscilloscope.cpp
//
// mt32-pi - A baremetal MIDI synthesizer for Raspberry Pi
// Oscilloscope visualization ported from GENaJam-Pi
//

#include "lcd/animations/oscilloscope.h"
#include "lcd/drivers/ssd1306.h"
#include "utility.h"
#include <circle/timer.h>
#include <circle/util.h>
#include <cmath>
#include <cstring>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Helper function to draw a line using Bresenham's algorithm
static void DrawLine(CSSD1306& OLED, int x0, int y0, int x1, int y1)
{
	int dx = abs(x1 - x0);
	int dy = abs(y1 - y0);
	int sx = (x0 < x1) ? 1 : -1;
	int sy = (y0 < y1) ? 1 : -1;
	int err = dx - dy;

	while (true)
	{
		OLED.SetPixel(x0, y0);

		if (x0 == x1 && y0 == y1)
			break;

		int e2 = 2 * err;
		if (e2 > -dy)
		{
			err -= dy;
			x0 += sx;
		}
		if (e2 < dx)
		{
			err += dx;
			y0 += sy;
		}
	}
}

COscilloscope::COscilloscope()
	: m_bInitialized(false),
	  m_nLastUpdateTime(0),
	  m_fGlobalPhase(0)
{
	memset(m_Channels, 0, sizeof(m_Channels));
}

void COscilloscope::Initialize()
{
	// Initialize all channels
	for (u8 i = 0; i < Channels; ++i)
	{
		m_Channels[i].nWaveform = i % (u8)TWaveform::Count;  // Distribute waveform types
		m_Channels[i].fFrequency = 1.0f + (i * 0.5f);  // Slightly different frequencies
		m_Channels[i].fAmplitude = 0.0f;
		m_Channels[i].fPhase = i * 0.5f;  // Phase offset per channel
		m_Channels[i].nDecay = 0;
		m_Channels[i].bActive = false;
	}

	m_fGlobalPhase = 0;
	m_bInitialized = true;
	m_nLastUpdateTime = CTimer::GetClockTicks();
}

void COscilloscope::HandleMidiNote(u8 nNote, u8 nVelocity, u8 nChannel)
{
	if (nChannel >= Channels)
		return;

	// Trigger the channel
	m_Channels[nChannel].bActive = true;
	m_Channels[nChannel].fAmplitude = nVelocity / 127.0f;
	m_Channels[nChannel].nDecay = 60 + (nVelocity / 3);  // Longer decay for louder notes

	// Map note to frequency (higher notes = more cycles visible)
	m_Channels[nChannel].fFrequency = 0.5f + (nNote / 40.0f);

	// Vary waveform based on note range
	if (nNote < 40)
	{
		m_Channels[nChannel].nWaveform = TWaveform::Sine;  // Bass = smooth sine
	}
	else if (nNote < 60)
	{
		m_Channels[nChannel].nWaveform = TWaveform::Triangle;  // Mid = triangle
	}
	else if (nNote < 80)
	{
		m_Channels[nChannel].nWaveform = TWaveform::Saw;  // Upper mid = saw
	}
	else if (nNote < 100)
	{
		m_Channels[nChannel].nWaveform = TWaveform::Square;  // High = square
	}
	else
	{
		m_Channels[nChannel].nWaveform = TWaveform::Noise;  // Very high = noise
	}
}

float COscilloscope::GetWaveValue(u8 nWaveform, float fPos)
{
	// Wrap position to 0-1 range
	fPos = fPos - floorf(fPos);

	switch (nWaveform)
	{
		case TWaveform::Sine:
			return sinf(fPos * 2.0f * M_PI);

		case TWaveform::Square:
			return (fPos < 0.5f) ? 1.0f : -1.0f;

		case TWaveform::Saw:
			return 2.0f * fPos - 1.0f;

		case TWaveform::Triangle:
			if (fPos < 0.25f) return fPos * 4.0f;
			else if (fPos < 0.75f) return 2.0f - fPos * 4.0f;
			else return fPos * 4.0f - 4.0f;

		case TWaveform::Noise:
			return ((float)((rand() % 201) - 100)) / 100.0f;

		default:
			return 0;
	}
}

void COscilloscope::Update(unsigned int nTicks)
{
	if (!m_bInitialized)
		Initialize();

	unsigned nNow = CTimer::GetClockTicks();
	unsigned nDeltaMs = Utility::TicksToMillis(nNow - m_nLastUpdateTime);

	// Rate limit updates (~40fps)
	if (nDeltaMs < 25)
		return;

	m_nLastUpdateTime = nNow;

	// Advance global phase for animation
	m_fGlobalPhase += 0.08f;
	if (m_fGlobalPhase > 1000.0f)
		m_fGlobalPhase = 0;

	// Update each channel
	for (u8 i = 0; i < Channels; ++i)
	{
		if (m_Channels[i].bActive)
		{
			// Decay amplitude over time
			if (m_Channels[i].nDecay > 0)
			{
				m_Channels[i].nDecay--;
				m_Channels[i].fAmplitude = m_Channels[i].nDecay / 80.0f;
				if (m_Channels[i].fAmplitude > 1.0f)
					m_Channels[i].fAmplitude = 1.0f;
			}
			else
			{
				m_Channels[i].fAmplitude *= 0.95f;  // Smooth decay
				if (m_Channels[i].fAmplitude < 0.02f)
				{
					m_Channels[i].fAmplitude = 0;
					m_Channels[i].bActive = false;
				}
			}
		}
	}
}

void COscilloscope::Draw(CSSD1306& OLED)
{
	// Draw center line (zero crossing reference)
	for (int x = 0; x < 128; x += 4)
	{
		OLED.SetPixel(x, (int)CenterY);
	}

	// Draw grid lines
	for (int x = 0; x < 128; x += 32)
	{
		for (int y = 4; y < 28; y += 4)
		{
			OLED.SetPixel(x, y);
		}
	}

	// Calculate combined waveform from all active channels
	float prev_y = CenterY;
	bool first_point = true;

	for (int x = 0; x < 128; ++x)
	{
		float combined = 0;
		float total_amp = 0;

		// Sum all active channel waveforms
		for (u8 ch = 0; ch < Channels; ++ch)
		{
			if (m_Channels[ch].fAmplitude > 0.01f)
			{
				float pos = (x / ScreenWidth) * m_Channels[ch].fFrequency +
				           m_fGlobalPhase + m_Channels[ch].fPhase;
				float wave = GetWaveValue(m_Channels[ch].nWaveform, pos);
				combined += wave * m_Channels[ch].fAmplitude;
				total_amp += m_Channels[ch].fAmplitude;
			}
		}

		// Normalize if multiple channels active
		if (total_amp > 1.0f)
		{
			combined /= total_amp;
		}

		// If no channels active, show a quiet baseline with subtle noise
		if (total_amp < 0.01f)
		{
			combined = ((float)((rand() % 11) - 5)) / 100.0f;  // Subtle noise floor
		}

		// Scale to screen coordinates
		float y = CenterY - (combined * 14.0f);  // 14 pixels amplitude (28 total height)

		// Clamp to screen
		if (y < 1) y = 1;
		if (y > 30) y = 30;

		// Draw connected line for smooth waveform
		if (!first_point)
		{
			DrawLine(OLED, x - 1, (int)prev_y, x, (int)y);
		}
		prev_y = y;
		first_point = false;
	}
}
