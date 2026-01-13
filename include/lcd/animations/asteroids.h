//
// asteroids.h
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

#ifndef _asteroids_h
#define _asteroids_h

#include <circle/types.h>

class CSSD1306;

// Asteroids game visualization - MIDI-reactive asteroids with AI ship
class CAsteroids
{
public:
	CAsteroids();

	void Initialize();
	void Update(unsigned int nTicks);
	void Draw(CSSD1306& OLED);
	void HandleMidiNote(u8 nNote, u8 nVelocity);

private:
	static constexpr u8 MaxAsteroids = 15;
	static constexpr u8 MaxBullets = 10;
	static constexpr float ScreenWidth = 128.0f;
	static constexpr float ScreenHeight = 32.0f;

	struct Ship
	{
		float x, y;           // Position
		float vx, vy;         // Velocity
		float angle;          // Rotation angle in radians
		bool bThrusting;      // Thrust state
		unsigned nLastShotTime;  // Timestamp of last shot
	};

	struct Bullet
	{
		float x, y;           // Position
		float vx, vy;         // Velocity
		unsigned nSpawnTime;  // When bullet was created
		bool bActive;
	};

	struct Asteroid
	{
		float x, y;           // Position
		float vx, vy;         // Velocity
		float angle;          // Rotation angle
		u8 nSize;             // 0=large, 1=medium, 2=small
		bool bActive;
		bool bIsNote;         // True if spawned from MIDI note
		u8 nNotePitch;        // MIDI note that spawned this asteroid
		u8 nVelocity;         // MIDI velocity
		unsigned nSpawnTime;
	};

	Ship m_Ship;
	Bullet m_Bullets[MaxBullets];
	Asteroid m_Asteroids[MaxAsteroids];
	bool m_bInitialized;
	unsigned m_nLastUpdateTime;
	unsigned m_nLastAIThinkTime;

	void CreateNoteAsteroid(u8 nNote, u8 nVelocity);
	void UpdatePhysics(unsigned int nTicks);
	void UpdateAI(unsigned int nTicks);
	void DrawShip(CSSD1306& OLED);
	void DrawBullets(CSSD1306& OLED);
	void DrawAsteroids(CSSD1306& OLED);
	float WrapX(float x);
	float WrapY(float y);
	bool CheckCollision(float x1, float y1, float r1, float x2, float y2, float r2);
	void ShootBullet();
	void SplitAsteroid(u8 nIndex);
};

#endif
