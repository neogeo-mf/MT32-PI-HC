//
// asteroids.cpp
//
// mt32-pi - A baremetal MIDI synthesizer for Raspberry Pi
// Asteroids visualization ported from GENaJam-Pi
//

#include "lcd/animations/asteroids.h"
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

CAsteroids::CAsteroids()
	: m_bInitialized(false),
	  m_nLastUpdateTime(0),
	  m_nLastAIThinkTime(0)
{
	memset(&m_Ship, 0, sizeof(m_Ship));
	memset(m_Bullets, 0, sizeof(m_Bullets));
	memset(m_Asteroids, 0, sizeof(m_Asteroids));
}

void CAsteroids::Initialize()
{
	// Initialize ship at center
	m_Ship.x = ScreenWidth / 2.0f;
	m_Ship.y = ScreenHeight / 2.0f;
	m_Ship.vx = 0;
	m_Ship.vy = 0;
	m_Ship.angle = 0;
	m_Ship.bThrusting = false;
	m_Ship.nLastShotTime = 0;

	// Initialize bullets as inactive
	for (u8 i = 0; i < MaxBullets; ++i)
	{
		m_Bullets[i].bActive = false;
	}

	// Initialize asteroids as inactive
	for (u8 i = 0; i < MaxAsteroids; ++i)
	{
		m_Asteroids[i].bActive = false;
	}

	m_bInitialized = true;
	m_nLastUpdateTime = CTimer::GetClockTicks();
	m_nLastAIThinkTime = CTimer::GetClockTicks();
}

void CAsteroids::HandleMidiNote(u8 nNote, u8 nVelocity)
{
	CreateNoteAsteroid(nNote, nVelocity);
}

void CAsteroids::CreateNoteAsteroid(u8 nNote, u8 nVelocity)
{
	// Find an inactive asteroid slot
	for (u8 i = 0; i < MaxAsteroids; ++i)
	{
		if (!m_Asteroids[i].bActive)
		{
			// Spawn at random edge position
			float edge = (float)(rand() % 4);

			if (edge < 1.0f)  // Top
			{
				m_Asteroids[i].x = (float)(rand() % 128);
				m_Asteroids[i].y = 0;
			}
			else if (edge < 2.0f)  // Bottom
			{
				m_Asteroids[i].x = (float)(rand() % 128);
				m_Asteroids[i].y = ScreenHeight;
			}
			else if (edge < 3.0f)  // Left
			{
				m_Asteroids[i].x = 0;
				m_Asteroids[i].y = (float)(rand() % 32);
			}
			else  // Right
			{
				m_Asteroids[i].x = ScreenWidth;
				m_Asteroids[i].y = (float)(rand() % 32);
			}

			// Velocity based on MIDI note pitch (higher notes = faster)
			float speed = 0.3f + (nNote / 200.0f);
			float angle = ((float)(rand() % 360)) * (M_PI / 180.0f);
			m_Asteroids[i].vx = cosf(angle) * speed;
			m_Asteroids[i].vy = sinf(angle) * speed;

			m_Asteroids[i].angle = 0;
			m_Asteroids[i].nSize = 0;  // Start as large
			m_Asteroids[i].bActive = true;
			m_Asteroids[i].bIsNote = true;
			m_Asteroids[i].nNotePitch = nNote;
			m_Asteroids[i].nVelocity = nVelocity;
			m_Asteroids[i].nSpawnTime = CTimer::GetClockTicks();
			break;
		}
	}
}

void CAsteroids::Update(unsigned int nTicks)
{
	if (!m_bInitialized)
		Initialize();

	UpdatePhysics(nTicks);
	UpdateAI(nTicks);
}

void CAsteroids::UpdatePhysics(unsigned int nTicks)
{
	unsigned nNow = CTimer::GetClockTicks();
	unsigned nDeltaMs = Utility::TicksToMillis(nNow - m_nLastUpdateTime);

	// Rate limit to ~30fps
	if (nDeltaMs < 33)
		return;

	m_nLastUpdateTime = nNow;

	// Update ship
	if (m_Ship.bThrusting)
	{
		m_Ship.vx += cosf(m_Ship.angle) * 0.15f;
		m_Ship.vy += sinf(m_Ship.angle) * 0.15f;
	}

	// Apply drag
	m_Ship.vx *= 0.98f;
	m_Ship.vy *= 0.98f;

	// Limit ship velocity
	float speed = sqrtf(m_Ship.vx * m_Ship.vx + m_Ship.vy * m_Ship.vy);
	if (speed > 2.0f)
	{
		m_Ship.vx = (m_Ship.vx / speed) * 2.0f;
		m_Ship.vy = (m_Ship.vy / speed) * 2.0f;
	}

	// Update ship position
	m_Ship.x += m_Ship.vx;
	m_Ship.y += m_Ship.vy;
	m_Ship.x = WrapX(m_Ship.x);
	m_Ship.y = WrapY(m_Ship.y);

	// Update bullets
	for (u8 i = 0; i < MaxBullets; ++i)
	{
		if (m_Bullets[i].bActive)
		{
			m_Bullets[i].x += m_Bullets[i].vx;
			m_Bullets[i].y += m_Bullets[i].vy;

			// Check if bullet is off screen or too old
			if (m_Bullets[i].x < 0 || m_Bullets[i].x > ScreenWidth ||
			    m_Bullets[i].y < 0 || m_Bullets[i].y > ScreenHeight ||
			    Utility::TicksToMillis(nNow - m_Bullets[i].nSpawnTime) > 1000)
			{
				m_Bullets[i].bActive = false;
			}
		}
	}

	// Update asteroids
	for (u8 i = 0; i < MaxAsteroids; ++i)
	{
		if (m_Asteroids[i].bActive)
		{
			m_Asteroids[i].x += m_Asteroids[i].vx;
			m_Asteroids[i].y += m_Asteroids[i].vy;
			m_Asteroids[i].x = WrapX(m_Asteroids[i].x);
			m_Asteroids[i].y = WrapY(m_Asteroids[i].y);
			m_Asteroids[i].angle += 0.05f;
		}
	}

	// Check bullet-asteroid collisions
	for (u8 i = 0; i < MaxBullets; ++i)
	{
		if (!m_Bullets[i].bActive)
			continue;

		for (u8 j = 0; j < MaxAsteroids; ++j)
		{
			if (!m_Asteroids[j].bActive)
				continue;

			float asteroidRadius = (m_Asteroids[j].nSize == 0) ? 6.0f :
			                        (m_Asteroids[j].nSize == 1) ? 4.0f : 2.0f;

			if (CheckCollision(m_Bullets[i].x, m_Bullets[i].y, 1.0f,
			                   m_Asteroids[j].x, m_Asteroids[j].y, asteroidRadius))
			{
				m_Bullets[i].bActive = false;
				SplitAsteroid(j);
				break;
			}
		}
	}
}

void CAsteroids::UpdateAI(unsigned int nTicks)
{
	unsigned nNow = CTimer::GetClockTicks();
	unsigned nDeltaMs = Utility::TicksToMillis(nNow - m_nLastAIThinkTime);

	// AI thinks every 100ms
	if (nDeltaMs < 100)
		return;

	m_nLastAIThinkTime = nNow;

	// Find nearest asteroid
	float nearestDist = 9999.0f;
	float targetX = 0, targetY = 0;
	bool bFoundTarget = false;

	for (u8 i = 0; i < MaxAsteroids; ++i)
	{
		if (m_Asteroids[i].bActive)
		{
			float dx = m_Asteroids[i].x - m_Ship.x;
			float dy = m_Asteroids[i].y - m_Ship.y;
			float dist = sqrtf(dx * dx + dy * dy);

			if (dist < nearestDist)
			{
				nearestDist = dist;
				targetX = m_Asteroids[i].x;
				targetY = m_Asteroids[i].y;
				bFoundTarget = true;
			}
		}
	}

	if (bFoundTarget)
	{
		// Calculate angle to target
		float dx = targetX - m_Ship.x;
		float dy = targetY - m_Ship.y;
		float targetAngle = atan2f(dy, dx);

		// Rotate ship towards target
		float angleDiff = targetAngle - m_Ship.angle;

		// Normalize angle difference to -PI to PI
		while (angleDiff > M_PI) angleDiff -= 2.0f * M_PI;
		while (angleDiff < -M_PI) angleDiff += 2.0f * M_PI;

		if (fabs(angleDiff) > 0.1f)
		{
			if (angleDiff > 0)
				m_Ship.angle += 0.15f;
			else
				m_Ship.angle -= 0.15f;
		}

		// Shoot if aimed at target
		if (fabs(angleDiff) < 0.3f && nearestDist < 50.0f)
		{
			ShootBullet();
		}

		// Thrust if asteroid is approaching
		if (nearestDist < 40.0f)
		{
			m_Ship.bThrusting = true;
		}
		else
		{
			m_Ship.bThrusting = false;
		}
	}
}

void CAsteroids::Draw(CSSD1306& OLED)
{
	DrawAsteroids(OLED);
	DrawBullets(OLED);
	DrawShip(OLED);
}

void CAsteroids::DrawShip(CSSD1306& OLED)
{
	// Draw simple triangle ship
	float shipSize = 4.0f;

	// Ship vertices (triangle pointing in direction of angle)
	float x1 = m_Ship.x + cosf(m_Ship.angle) * shipSize;
	float y1 = m_Ship.y + sinf(m_Ship.angle) * shipSize;

	float x2 = m_Ship.x + cosf(m_Ship.angle + 2.6f) * shipSize;
	float y2 = m_Ship.y + sinf(m_Ship.angle + 2.6f) * shipSize;

	float x3 = m_Ship.x + cosf(m_Ship.angle - 2.6f) * shipSize;
	float y3 = m_Ship.y + sinf(m_Ship.angle - 2.6f) * shipSize;

	DrawLine(OLED, (int)x1, (int)y1, (int)x2, (int)y2);
	DrawLine(OLED, (int)x2, (int)y2, (int)x3, (int)y3);
	DrawLine(OLED, (int)x3, (int)y3, (int)x1, (int)y1);

	// Draw thrust flame
	if (m_Ship.bThrusting)
	{
		float fx = m_Ship.x - cosf(m_Ship.angle) * 5.0f;
		float fy = m_Ship.y - sinf(m_Ship.angle) * 5.0f;
		OLED.SetPixel((int)fx, (int)fy);
	}
}

void CAsteroids::DrawBullets(CSSD1306& OLED)
{
	for (u8 i = 0; i < MaxBullets; ++i)
	{
		if (m_Bullets[i].bActive)
		{
			OLED.SetPixel((int)m_Bullets[i].x, (int)m_Bullets[i].y);
		}
	}
}

void CAsteroids::DrawAsteroids(CSSD1306& OLED)
{
	for (u8 i = 0; i < MaxAsteroids; ++i)
	{
		if (m_Asteroids[i].bActive)
		{
			int radius = (m_Asteroids[i].nSize == 0) ? 6 :
			             (m_Asteroids[i].nSize == 1) ? 4 : 2;

			// Draw irregular asteroid shape
			int sides = 6;
			for (int s = 0; s < sides; ++s)
			{
				float a1 = m_Asteroids[i].angle + (s * 2.0f * M_PI / sides);
				float a2 = m_Asteroids[i].angle + ((s + 1) * 2.0f * M_PI / sides);

				int x1 = (int)(m_Asteroids[i].x + cosf(a1) * radius);
				int y1 = (int)(m_Asteroids[i].y + sinf(a1) * radius);
				int x2 = (int)(m_Asteroids[i].x + cosf(a2) * radius);
				int y2 = (int)(m_Asteroids[i].y + sinf(a2) * radius);

				DrawLine(OLED, x1, y1, x2, y2);
			}
		}
	}
}

float CAsteroids::WrapX(float x)
{
	while (x < 0) x += ScreenWidth;
	while (x >= ScreenWidth) x -= ScreenWidth;
	return x;
}

float CAsteroids::WrapY(float y)
{
	while (y < 0) y += ScreenHeight;
	while (y >= ScreenHeight) y -= ScreenHeight;
	return y;
}

bool CAsteroids::CheckCollision(float x1, float y1, float r1, float x2, float y2, float r2)
{
	float dx = x2 - x1;
	float dy = y2 - y1;
	float dist = sqrtf(dx * dx + dy * dy);
	return dist < (r1 + r2);
}

void CAsteroids::ShootBullet()
{
	unsigned nNow = CTimer::GetClockTicks();

	// Rate limit shots
	if (Utility::TicksToMillis(nNow - m_Ship.nLastShotTime) < 300)
		return;

	m_Ship.nLastShotTime = nNow;

	// Find inactive bullet
	for (u8 i = 0; i < MaxBullets; ++i)
	{
		if (!m_Bullets[i].bActive)
		{
			m_Bullets[i].x = m_Ship.x;
			m_Bullets[i].y = m_Ship.y;
			m_Bullets[i].vx = cosf(m_Ship.angle) * 3.0f;
			m_Bullets[i].vy = sinf(m_Ship.angle) * 3.0f;
			m_Bullets[i].nSpawnTime = nNow;
			m_Bullets[i].bActive = true;
			break;
		}
	}
}

void CAsteroids::SplitAsteroid(u8 nIndex)
{
	if (!m_Asteroids[nIndex].bActive)
		return;

	u8 nSize = m_Asteroids[nIndex].nSize;
	float x = m_Asteroids[nIndex].x;
	float y = m_Asteroids[nIndex].y;

	// Deactivate parent asteroid
	m_Asteroids[nIndex].bActive = false;

	// Don't split small asteroids
	if (nSize >= 2)
		return;

	// Create 2 smaller asteroids
	for (u8 i = 0; i < 2; ++i)
	{
		for (u8 j = 0; j < MaxAsteroids; ++j)
		{
			if (!m_Asteroids[j].bActive)
			{
				m_Asteroids[j].x = x;
				m_Asteroids[j].y = y;
				m_Asteroids[j].nSize = nSize + 1;

				float angle = ((float)(rand() % 360)) * (M_PI / 180.0f);
				float speed = 0.5f + ((float)(rand() % 100)) / 100.0f;
				m_Asteroids[j].vx = cosf(angle) * speed;
				m_Asteroids[j].vy = sinf(angle) * speed;

				m_Asteroids[j].angle = 0;
				m_Asteroids[j].bActive = true;
				m_Asteroids[j].bIsNote = false;
				m_Asteroids[j].nSpawnTime = CTimer::GetClockTicks();
				break;
			}
		}
	}
}
