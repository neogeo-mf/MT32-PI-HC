//
// menu.cpp
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

#include "menu.h"
#include "lcd/lcd.h"
#include "lcd/drivers/ssd1306.h"
#include "utility.h"
#include <circle/timer.h>
#include <fatfs/ff.h>
#include <cstdio>
#include <cstring>
#include <algorithm>

CMenu::CMenu()
	: m_bActive(false),
	  m_bEditing(false),
	  m_bShowAnimSettings(false),
	  m_bShowPresetMenu(false),
	  m_nSelectedChannel(0),
	  m_SelectedOption(TMenuOption::Channel),
	  m_AnimSettingsOption(TAnimSettingsOption::Mode),
	  m_VisualizationMode(TVisualizationMode::BarGraph),
	  m_PresetMenuOption(TPresetMenuOption::Save),
	  m_PresetMenuScreen(TPresetMenuScreen::Main),
	  m_PresetListOperation(TPresetListOperation::Load),
	  m_NameEditorOption(TNameEditorOption::Character0),
	  m_nLastSelectTime(0),
	  m_nLastEditToggleTime(0),
	  m_nEditToggleIndex(0),
	  m_nPresetListScroll(0),
	  m_nPresetListSelection(0),
	  m_nPresetCount(0),
	  m_bConfirmYes(true),
	  m_nMutedChannels(0),
	  m_bPendingProgramChange(false),
	  m_nProgramChangeChannel(0),
	  m_bSendAllPrograms(false)
{
	// Initialize toggle loop detection history
	for (u8 i = 0; i < 5; ++i)
		m_EditToggleHistory[i] = 0;

	// Initialize channel settings to defaults
	for (u8 i = 0; i < MIDIChannelCount; ++i)
	{
		m_ChannelRoute[i] = i;  // No routing by default
		m_ChannelVolume[i] = DefaultChannelVolume;
		m_ChannelProgram[i] = 0;  // Default program 0
	}

	// Initialize preset name buffer: first char 'A', rest spaces
	m_PresetNameBuffer[0] = 'A';
	for (u8 i = 1; i < 12; ++i)
		m_PresetNameBuffer[i] = ' ';
	m_PresetNameBuffer[12] = '\0';

	// Initialize preset list
	memset(m_PresetNames, 0, sizeof(m_PresetNames));
}

void CMenu::EnterMenu()
{
	m_bActive = true;
	m_bEditing = false;
	m_bShowAnimSettings = false;
	m_bShowPresetMenu = false;
	m_SelectedOption = TMenuOption::Channel;
	m_AnimSettingsOption = TAnimSettingsOption::Mode;
	m_PresetMenuOption = TPresetMenuOption::Save;
	m_PresetMenuScreen = TPresetMenuScreen::Main;
}

void CMenu::ExitMenu()
{
	m_bActive = false;
	m_bEditing = false;
	m_bShowAnimSettings = false;
	m_bShowPresetMenu = false;
}

void CMenu::Navigate(s8 nDelta)
{
	if (m_bShowPresetMenu)
	{
		NavigatePresetMenu(nDelta);
	}
	else if (m_bShowAnimSettings)
	{
		if (m_bEditing)
			AdjustAnimSettingsValue(nDelta);
		else
			NavigateAnimSettings(nDelta);
	}
	else
	{
		if (m_bEditing)
			AdjustValue(nDelta);
		else
			NavigateOptions(nDelta);
	}
}

void CMenu::Select()
{
	const unsigned nCurrentTime = CTimer::GetClockTicks();
	const unsigned nTimeSinceLastSelect = nCurrentTime - m_nLastSelectTime;
	const unsigned nTimeSinceLastToggle = nCurrentTime - m_nLastEditToggleTime;

	// CRITICAL: Absolute minimum time between ANY Select() calls
	// This protects against interrupt handler sampling GPIO during I2C display updates
	// Display I2C traffic induces voltage spikes on GPIO pins, causing phantom button presses
	constexpr unsigned nMinSelectIntervalMs = 400;
	if (Utility::TicksToMillis(nTimeSinceLastSelect) < nMinSelectIntervalMs)
		return;

	m_nLastSelectTime = nCurrentTime;

	// ADAPTIVE DEBOUNCING: Even longer lockout right after edit toggle
	// Display color inversion changes cause especially strong EMI
	constexpr unsigned nToggleLockoutMs = 600;
	if (m_nLastEditToggleTime != 0 && Utility::TicksToMillis(nTimeSinceLastToggle) < nToggleLockoutMs)
		return;

	// TOGGLE LOOP PREVENTION: Check if we're toggling too rapidly BEFORE processing
	// This prevents phantom click loops caused by EMI from display updates
	// Check the toggle history to see if we're in a rapid toggle pattern
	if (m_EditToggleHistory[0] != 0)  // Only check if we have history
	{
		// Find the oldest toggle in our circular buffer
		unsigned nOldestToggle = m_EditToggleHistory[m_nEditToggleIndex];
		if (nOldestToggle != 0)
		{
			unsigned nToggleSpan = Utility::TicksToMillis(nCurrentTime - nOldestToggle);
			// If last 5 toggles happened within 1200ms, we're likely in a phantom loop
			// PREVENT this click from being processed by exiting early
			if (nToggleSpan < 1200)
			{
				// PREVENTION: Don't process this click - exit menu to break the loop
				ExitMenu();
				return;
			}
		}
	}

	if (!m_bActive)
	{
		// Enter menu
		EnterMenu();
	}
	else if (m_bShowPresetMenu)
	{
		// Handle preset menu - implementation depends on current screen
		switch (m_PresetMenuScreen)
		{
			case TPresetMenuScreen::Main:
			{
				// Handle main preset menu options
				if (m_PresetMenuOption == TPresetMenuOption::Save)
				{
					// Go to name editor
					m_PresetMenuScreen = TPresetMenuScreen::NameEditor;
					m_bEditing = false;
					m_NameEditorOption = TNameEditorOption::Character0;
					// Reset name buffer: first char 'A', rest spaces
					m_PresetNameBuffer[0] = 'A';
					for (u8 i = 1; i < 12; ++i)
						m_PresetNameBuffer[i] = ' ';
					m_PresetNameBuffer[12] = '\0';
				}
				else if (m_PresetMenuOption == TPresetMenuOption::Load)
				{
					// Go to preset list for loading
					LoadPresetList();
					if (m_nPresetCount > 0)
					{
						m_PresetMenuScreen = TPresetMenuScreen::PresetList;
						m_PresetListOperation = TPresetListOperation::Load;
						m_nPresetListSelection = 0;
						m_nPresetListScroll = 0;
					}
				}
				else if (m_PresetMenuOption == TPresetMenuOption::Delete)
				{
					// Go to preset list for deleting
					LoadPresetList();
					if (m_nPresetCount > 0)
					{
						m_PresetMenuScreen = TPresetMenuScreen::PresetList;
						m_PresetListOperation = TPresetListOperation::Delete;
						m_nPresetListSelection = 0;
						m_nPresetListScroll = 0;
					}
				}
				else if (m_PresetMenuOption == TPresetMenuOption::Back)
				{
					// Go back to animation settings
					m_bShowPresetMenu = false;
					m_bShowAnimSettings = true;
					m_bEditing = false;
					m_AnimSettingsOption = TAnimSettingsOption::Mode;
				}
				else if (m_PresetMenuOption == TPresetMenuOption::Exit)
				{
					// Exit menu entirely
					ExitMenu();
				}
				break;
			}

			case TPresetMenuScreen::NameEditor:
			{
				// Handle name editor selections
				if (m_NameEditorOption == TNameEditorOption::Save)
				{
					// SAVE button pressed
					// Check if preset exists
					if (PresetExists(m_PresetNameBuffer))
					{
						// Go to confirmation screen
						m_PresetMenuScreen = TPresetMenuScreen::Confirm;
						m_bConfirmYes = true;
					}
					else
					{
						// Save directly
						if (SavePreset(m_PresetNameBuffer))
						{
							// Success - go back to main preset menu
							m_PresetMenuScreen = TPresetMenuScreen::Main;
							LoadPresetList();
						}
						// else: Show error (TODO: add error display)
					}
				}
				else if (m_NameEditorOption == TNameEditorOption::Cancel)
				{
					// CANCEL button pressed - go back to preset menu
					m_PresetMenuScreen = TPresetMenuScreen::Main;
				}
				else
				{
					// Character position selected - toggle edit mode
					// Track this toggle for prevention logic
					m_EditToggleHistory[m_nEditToggleIndex] = nCurrentTime;
					m_nEditToggleIndex = (m_nEditToggleIndex + 1) % 5;
					m_nLastEditToggleTime = nCurrentTime;  // Record for adaptive debouncing
					m_bEditing = !m_bEditing;
				}
				break;
			}

			case TPresetMenuScreen::PresetList:
			{
				// Select preset from list
				if (m_nPresetCount == 0)
				{
					// No presets, go back
					m_PresetMenuScreen = TPresetMenuScreen::Main;
				}
				else if (m_PresetListOperation == TPresetListOperation::Load)
				{
					// Load selected preset
					if (m_nPresetListSelection < m_nPresetCount)
					{
						if (LoadPreset(m_PresetNames[m_nPresetListSelection]))
						{
							// Success - exit menu to show changes
							ExitMenu();
						}
						// else: Show error (TODO: add error display)
					}
				}
				else if (m_PresetListOperation == TPresetListOperation::Delete)
				{
					// Check if trying to delete DEFAULT
					if (strcmp(m_PresetNames[m_nPresetListSelection], "DEFAULT") == 0)
					{
						// Cannot delete DEFAULT - do nothing or show error
						// For now, just ignore
					}
					else
					{
						// Go to confirmation screen
						m_PresetMenuScreen = TPresetMenuScreen::Confirm;
						m_bConfirmYes = false;  // Default to NO for delete
					}
				}
				break;
			}

			case TPresetMenuScreen::Confirm:
			{
				// Handle confirmation
				if (m_bConfirmYes)
				{
					// User confirmed
					if (m_PresetListOperation == TPresetListOperation::Delete)
					{
						// Delete the preset
						if (m_nPresetListSelection < m_nPresetCount)
						{
							DeletePreset(m_PresetNames[m_nPresetListSelection]);
							LoadPresetList();

							// Go back to main menu
							m_PresetMenuScreen = TPresetMenuScreen::Main;
						}
					}
					else
					{
						// This is an overwrite confirmation from name editor
						if (SavePreset(m_PresetNameBuffer))
						{
							// Success - go back to main preset menu
							m_PresetMenuScreen = TPresetMenuScreen::Main;
							LoadPresetList();
						}
					}
				}
				else
				{
					// User cancelled
					if (m_PresetListOperation == TPresetListOperation::Delete)
					{
						// Go back to preset list
						m_PresetMenuScreen = TPresetMenuScreen::PresetList;
					}
					else
					{
						// Go back to name editor
						m_PresetMenuScreen = TPresetMenuScreen::NameEditor;
					}
				}
				break;
			}
		}
	}
	else if (m_bShowAnimSettings)
	{
		// Handle animation settings screen
		if (m_AnimSettingsOption == TAnimSettingsOption::Next)
		{
			// Go to preset menu
			m_bShowAnimSettings = false;
			m_bShowPresetMenu = true;
			m_bEditing = false;
			m_PresetMenuScreen = TPresetMenuScreen::Main;
			m_PresetMenuOption = TPresetMenuOption::Save;
			LoadPresetList();
		}
		else if (m_AnimSettingsOption == TAnimSettingsOption::Back)
		{
			// Go back to main menu
			m_bShowAnimSettings = false;
			m_bEditing = false;
		}
		else if (m_AnimSettingsOption == TAnimSettingsOption::Exit)
		{
			// Exit menu entirely
			ExitMenu();
		}
		else
		{
			// Toggle edit mode for Mode
			// Track this toggle for prevention logic
			m_EditToggleHistory[m_nEditToggleIndex] = nCurrentTime;
			m_nEditToggleIndex = (m_nEditToggleIndex + 1) % 5;
			m_nLastEditToggleTime = nCurrentTime;  // Record for adaptive debouncing
			m_bEditing = !m_bEditing;
		}
	}
	else if (m_SelectedOption == TMenuOption::Next)
	{
		// Go to animation settings screen
		m_bShowAnimSettings = true;
		m_bEditing = false;
		m_AnimSettingsOption = TAnimSettingsOption::Mode;
	}
	else if (m_SelectedOption == TMenuOption::Exit)
	{
		// Exit menu
		ExitMenu();
	}
	else
	{
		// Toggle edit mode for Channel, Mute, Route, Volume, Program
		// Track this toggle for prevention logic
		m_EditToggleHistory[m_nEditToggleIndex] = nCurrentTime;
		m_nEditToggleIndex = (m_nEditToggleIndex + 1) % 5;
		m_nLastEditToggleTime = nCurrentTime;  // Record for adaptive debouncing
		m_bEditing = !m_bEditing;
	}
}

void CMenu::NavigateOptions(s8 nDelta)
{
	s8 nOption = static_cast<s8>(m_SelectedOption) + nDelta;

	// Wrap around
	if (nOption < 0)
		nOption = static_cast<s8>(TMenuOption::Count) - 1;
	else if (nOption >= static_cast<s8>(TMenuOption::Count))
		nOption = 0;

	m_SelectedOption = static_cast<TMenuOption>(nOption);
}

void CMenu::NavigateAnimSettings(s8 nDelta)
{
	s8 nOption = static_cast<s8>(m_AnimSettingsOption) + nDelta;

	// Wrap around
	if (nOption < 0)
		nOption = static_cast<s8>(TAnimSettingsOption::Count) - 1;
	else if (nOption >= static_cast<s8>(TAnimSettingsOption::Count))
		nOption = 0;

	m_AnimSettingsOption = static_cast<TAnimSettingsOption>(nOption);
}

void CMenu::AdjustValue(s8 nDelta)
{
	switch (m_SelectedOption)
	{
		case TMenuOption::Channel:
		{
			s16 nNewChannel = m_nSelectedChannel + nDelta;
			if (nNewChannel < 0)
				nNewChannel = MIDIChannelCount - 1;
			else if (nNewChannel >= MIDIChannelCount)
				nNewChannel = 0;
			m_nSelectedChannel = static_cast<u8>(nNewChannel);
			break;
		}

		case TMenuOption::Mute:
			// Toggle mute on any rotation
			if (nDelta != 0)
			{
				u16 mask = 1 << m_nSelectedChannel;
				m_nMutedChannels ^= mask;
			}
			break;

		case TMenuOption::Route:
		{
			s16 nNewRoute = m_ChannelRoute[m_nSelectedChannel] + nDelta;
			if (nNewRoute < 0)
				nNewRoute = MIDIChannelCount - 1;
			else if (nNewRoute >= MIDIChannelCount)
				nNewRoute = 0;
			m_ChannelRoute[m_nSelectedChannel] = static_cast<u8>(nNewRoute);
			break;
		}

		case TMenuOption::Volume:
		{
			s16 nNewVolume = m_ChannelVolume[m_nSelectedChannel] + (nDelta * 5);
			if (nNewVolume < 0)
				nNewVolume = 0;
			else if (nNewVolume > 127)
				nNewVolume = 127;
			m_ChannelVolume[m_nSelectedChannel] = static_cast<u8>(nNewVolume);
			break;
		}

		case TMenuOption::Program:
		{
			s16 nNewProgram = m_ChannelProgram[m_nSelectedChannel] + nDelta;
			if (nNewProgram < 0)
				nNewProgram = 127;
			else if (nNewProgram > 127)
				nNewProgram = 0;
			m_ChannelProgram[m_nSelectedChannel] = static_cast<u8>(nNewProgram);

			// Mark that this channel needs a program change message sent
			m_bPendingProgramChange = true;
			m_nProgramChangeChannel = m_nSelectedChannel;
			break;
		}

		default:
			break;
	}
}

void CMenu::AdjustAnimSettingsValue(s8 nDelta)
{
	if (m_AnimSettingsOption == TAnimSettingsOption::Mode && nDelta != 0)
	{
		// Toggle between BarGraph and Animation
		s8 nMode = static_cast<s8>(m_VisualizationMode) + nDelta;
		if (nMode < 0)
			nMode = static_cast<s8>(TVisualizationMode::Count) - 1;
		else if (nMode >= static_cast<s8>(TVisualizationMode::Count))
			nMode = 0;
		m_VisualizationMode = static_cast<TVisualizationMode>(nMode);
	}
}

void CMenu::Draw(CLCD& LCD) const
{
	if (!m_bActive)
		return;

	if (m_bShowPresetMenu)
		DrawPresetMenu(LCD);
	else if (m_bShowAnimSettings)
		DrawAnimSettings(LCD);
	else
		DrawMainMenu(LCD);
}

void CMenu::DrawMainMenu(CLCD& LCD) const
{
	if (!m_bActive)
		return;

	const bool bGraphical = (LCD.GetType() == CLCD::TType::Graphical);

	if (bGraphical)
	{
		// OLED Display - 3 row layout with small font
		// Row 0 (y=0):  "MIDI CH. SETUP" title
		// Row 1 (y=11): "Ch:" + channel + "M:" + mute + "Vo:" + volume
		// Row 2 (y=21): "Pr:" + program + "RT:" + route + → + X

		CSSD1306& OLED = static_cast<CSSD1306&>(LCD);

		// Don't clear - parent UI already cleared the display

		const u8 nChannel = (m_nSelectedChannel < MIDIChannelCount) ? m_nSelectedChannel : 0;
		const bool bMuted = IsChannelMuted(nChannel);
		const u8 nRoute = m_ChannelRoute[nChannel];
		const u8 nVolume = m_ChannelVolume[nChannel];
		const u8 nProgram = m_ChannelProgram[nChannel];

		// Coordinates
		const int16_t y0 = 0;
		const int16_t y1 = 11;
		const int16_t y2 = 21;
		const int16_t channelValueX = 18;
		const int16_t muteX = 44;
		const int16_t muteValueX = 56;
		const int16_t volumeX = 78;
		const int16_t volumeValueX = 96;
		const int16_t programValueX = 18;
		const int16_t routeX = 48;
		const int16_t routeValueX = 66;
		const int16_t nextX = 90;
		const int16_t exitX = 108;

		// Row 0: Title
		OLED.PrintSmall("MIDI CH. SETUP", 0, y0, false);

		// Row 1: Channel
		OLED.PrintSmall("Ch:", 0, y1, false);
		bool channelSelected = (m_SelectedOption == TMenuOption::Channel);
		char chStr[4];
		snprintf(chStr, sizeof(chStr), "%2d", m_nSelectedChannel + 1);

		if (channelSelected)
		{
			if (m_bEditing)
			{
				// Fill entire box area for editing
				OLED.DrawFilledRect(channelValueX, y1, channelValueX + 17, y1 + 7, false);
			}
			else
			{
				// Draw outline only when selected but not editing
				OLED.DrawFilledRect(channelValueX, y1 - 1, channelValueX + 17, y1 - 1, false);  // Top
				OLED.DrawFilledRect(channelValueX, y1 + 8, channelValueX + 17, y1 + 8, false);  // Bottom
				OLED.DrawFilledRect(channelValueX, y1 - 1, channelValueX, y1 + 8, false);  // Left
				OLED.DrawFilledRect(channelValueX + 17, y1 - 1, channelValueX + 17, y1 + 8, false);  // Right
			}
		}
		OLED.PrintSmall(chStr, channelValueX + 2, y1, channelSelected && m_bEditing);

		// Mute
		OLED.PrintSmall("M:", muteX, y1, false);
		bool muteSelected = (m_SelectedOption == TMenuOption::Mute);
		const char* muteStr = bMuted ? "ON" : "OFF";

		if (muteSelected)
		{
			if (m_bEditing)
			{
				OLED.DrawFilledRect(muteValueX, y1, muteValueX + 17, y1 + 7, false);
			}
			else
			{
				OLED.DrawFilledRect(muteValueX, y1 - 1, muteValueX + 17, y1 - 1, false);
				OLED.DrawFilledRect(muteValueX, y1 + 8, muteValueX + 17, y1 + 8, false);
				OLED.DrawFilledRect(muteValueX, y1 - 1, muteValueX, y1 + 8, false);
				OLED.DrawFilledRect(muteValueX + 17, y1 - 1, muteValueX + 17, y1 + 8, false);
			}
		}
		OLED.PrintSmall(muteStr, muteValueX, y1, muteSelected && m_bEditing);

		// Volume
		OLED.PrintSmall("Vo:", volumeX, y1, false);
		bool volumeSelected = (m_SelectedOption == TMenuOption::Volume);
		char volStr[4];
		snprintf(volStr, sizeof(volStr), "%3d", nVolume);

		if (volumeSelected)
		{
			if (m_bEditing)
			{
				OLED.DrawFilledRect(volumeValueX, y1, volumeValueX + 17, y1 + 7, false);
			}
			else
			{
				OLED.DrawFilledRect(volumeValueX, y1 - 1, volumeValueX + 17, y1 - 1, false);
				OLED.DrawFilledRect(volumeValueX, y1 + 8, volumeValueX + 17, y1 + 8, false);
				OLED.DrawFilledRect(volumeValueX, y1 - 1, volumeValueX, y1 + 8, false);
				OLED.DrawFilledRect(volumeValueX + 17, y1 - 1, volumeValueX + 17, y1 + 8, false);
			}
		}
		OLED.PrintSmall(volStr, volumeValueX, y1, volumeSelected && m_bEditing);

		// Row 2: Program
		OLED.PrintSmall("Pr:", 0, y2, false);
		bool programSelected = (m_SelectedOption == TMenuOption::Program);
		char progStr[4];
		snprintf(progStr, sizeof(progStr), "%3d", nProgram);

		if (programSelected)
		{
			if (m_bEditing)
			{
				OLED.DrawFilledRect(programValueX, y2, programValueX + 17, y2 + 7, false);
			}
			else
			{
				OLED.DrawFilledRect(programValueX, y2 - 1, programValueX + 17, y2 - 1, false);
				OLED.DrawFilledRect(programValueX, y2 + 8, programValueX + 17, y2 + 8, false);
				OLED.DrawFilledRect(programValueX, y2 - 1, programValueX, y2 + 8, false);
				OLED.DrawFilledRect(programValueX + 17, y2 - 1, programValueX + 17, y2 + 8, false);
			}
		}
		OLED.PrintSmall(progStr, programValueX, y2, programSelected && m_bEditing);

		// Route
		OLED.PrintSmall("RT:", routeX, y2, false);
		bool routeSelected = (m_SelectedOption == TMenuOption::Route);
		char rteStr[4];
		if (nRoute == m_nSelectedChannel)
			snprintf(rteStr, sizeof(rteStr), "--");
		else
			snprintf(rteStr, sizeof(rteStr), "%2d", nRoute + 1);

		if (routeSelected)
		{
			if (m_bEditing)
			{
				OLED.DrawFilledRect(routeValueX, y2, routeValueX + 11, y2 + 7, false);
			}
			else
			{
				OLED.DrawFilledRect(routeValueX, y2 - 1, routeValueX + 11, y2 - 1, false);
				OLED.DrawFilledRect(routeValueX, y2 + 8, routeValueX + 11, y2 + 8, false);
				OLED.DrawFilledRect(routeValueX, y2 - 1, routeValueX, y2 + 8, false);
				OLED.DrawFilledRect(routeValueX + 11, y2 - 1, routeValueX + 11, y2 + 8, false);
			}
		}
		OLED.PrintSmall(rteStr, routeValueX, y2, routeSelected && m_bEditing);

		// NEXT button (→ arrow, Unicode right arrow character)
		bool nextSelected = (m_SelectedOption == TMenuOption::Next);
		if (nextSelected)
			OLED.DrawFilledRect(nextX, y2 - 1, nextX + 12 - 1, y2 + 8, false);
		OLED.PrintSmall(">", nextX + 2, y2, nextSelected);

		// EXIT button (X)
		bool exitSelected = (m_SelectedOption == TMenuOption::Exit);
		if (exitSelected)
			OLED.DrawFilledRect(exitX, y2 - 1, exitX + 12 - 1, y2 + 8, false);
		OLED.PrintSmall("X", exitX + 2, y2, exitSelected);

		// Don't call Flip() - parent UI will do it
	}
	else
	{
		// Character LCD - simpler 2-line display
		char line1[21];
		char line2[21];

		const u8 nChannel = (m_nSelectedChannel < MIDIChannelCount) ? m_nSelectedChannel : 0;
		const bool bMuted = IsChannelMuted(nChannel);
		const u8 nRoute = m_ChannelRoute[nChannel];
		const u8 nVolume = m_ChannelVolume[nChannel];
		const u8 nProgram = m_ChannelProgram[nChannel];

		// Show currently selected option
		switch (m_SelectedOption)
		{
			case TMenuOption::Channel:
				snprintf(line1, sizeof(line1), "Channel: %d", m_nSelectedChannel + 1);
				snprintf(line2, sizeof(line2), "%s", m_bEditing ? "> Editing <" : "");
				break;

			case TMenuOption::Mute:
				snprintf(line1, sizeof(line1), "Mute Ch%d", m_nSelectedChannel + 1);
				snprintf(line2, sizeof(line2), "%s", bMuted ? "ON" : "OFF");
				break;

			case TMenuOption::Route:
				snprintf(line1, sizeof(line1), "Route Ch%d", m_nSelectedChannel + 1);
				if (nRoute == m_nSelectedChannel)
					snprintf(line2, sizeof(line2), "None");
				else
					snprintf(line2, sizeof(line2), "-> Ch%d", nRoute + 1);
				break;

			case TMenuOption::Volume:
				snprintf(line1, sizeof(line1), "Volume Ch%d", m_nSelectedChannel + 1);
				snprintf(line2, sizeof(line2), "%d", nVolume);
				break;

			case TMenuOption::Program:
				snprintf(line1, sizeof(line1), "Program Ch%d", m_nSelectedChannel + 1);
				snprintf(line2, sizeof(line2), "%d", nProgram);
				break;

			case TMenuOption::Next:
				snprintf(line1, sizeof(line1), "Next Page");
				snprintf(line2, sizeof(line2), "Anim settings");
				break;

			case TMenuOption::Exit:
				snprintf(line1, sizeof(line1), "Exit Menu");
				snprintf(line2, sizeof(line2), "Press to exit");
				break;

			default:
				line1[0] = '\0';
				line2[0] = '\0';
				break;
		}

		LCD.Print(line1, 0, 0, true);
		LCD.Print(line2, 0, 1, true);
	}
}

bool CMenu::IsChannelMuted(u8 nChannel) const
{
	if (nChannel >= MIDIChannelCount)
		return false;

	return (m_nMutedChannels & (1 << nChannel)) != 0;
}

u8 CMenu::GetChannelRoute(u8 nChannel) const
{
	if (nChannel >= MIDIChannelCount)
		return nChannel;

	return m_ChannelRoute[nChannel];
}

u8 CMenu::GetChannelVolume(u8 nChannel) const
{
	if (nChannel >= MIDIChannelCount)
		return DefaultChannelVolume;

	return m_ChannelVolume[nChannel];
}

u8 CMenu::GetChannelProgram(u8 nChannel) const
{
	if (nChannel >= MIDIChannelCount)
		return 0;

	return m_ChannelProgram[nChannel];
}

void CMenu::DrawAnimSettings(CLCD& LCD) const
{
	const bool bGraphical = (LCD.GetType() == CLCD::TType::Graphical);

	if (bGraphical)
	{
		// OLED Display - animation settings screen
		// Row 0 (y=0):  "ANIM SETTINGS" title
		// Row 1 (y=11): "Mode:" + mode value
		// Row 2 (y=21): NEXT + BACK + EXIT buttons

		CSSD1306& OLED = static_cast<CSSD1306&>(LCD);

		const int16_t y0 = 0;
		const int16_t y1 = 11;
		const int16_t y2 = 21;
		const int16_t modeValueX = 30;
		const int16_t nextX = 90;
		const int16_t backX = 72;
		const int16_t exitX = 108;

		// Row 0: Title
		OLED.PrintSmall("ANIM SETTINGS", 0, y0, false);

		// Row 1: Mode
		OLED.PrintSmall("Mode:", 0, y1, false);
		bool modeSelected = (m_AnimSettingsOption == TAnimSettingsOption::Mode);
		const char* modeStr = "Bar Graph";
		switch (m_VisualizationMode)
		{
			case TVisualizationMode::BarGraph:
				modeStr = "Bar Graph";
				break;
			case TVisualizationMode::Animation:
				modeStr = "Animation";
				break;
			case TVisualizationMode::Asteroids:
				modeStr = "Asteroids";
				break;
			case TVisualizationMode::MatrixRain:
				modeStr = "Matrix Rain";
				break;
			case TVisualizationMode::Oscilloscope:
				modeStr = "Oscilloscope";
				break;
			default:
				modeStr = "Unknown";
				break;
		}

		if (modeSelected)
		{
			if (m_bEditing)
			{
				// Fill entire box area for editing
				OLED.DrawFilledRect(modeValueX, y1, modeValueX + 53, y1 + 7, false);
			}
			else
			{
				// Draw outline only when selected but not editing
				OLED.DrawFilledRect(modeValueX, y1 - 1, modeValueX + 53, y1 - 1, false);  // Top
				OLED.DrawFilledRect(modeValueX, y1 + 8, modeValueX + 53, y1 + 8, false);  // Bottom
				OLED.DrawFilledRect(modeValueX, y1 - 1, modeValueX, y1 + 8, false);  // Left
				OLED.DrawFilledRect(modeValueX + 53, y1 - 1, modeValueX + 53, y1 + 8, false);  // Right
			}
		}
		OLED.PrintSmall(modeStr, modeValueX + 2, y1, modeSelected && m_bEditing);

		// Row 2: BACK button (<)
		bool backSelected = (m_AnimSettingsOption == TAnimSettingsOption::Back);
		if (backSelected)
			OLED.DrawFilledRect(backX, y2 - 1, backX + 12 - 1, y2 + 8, false);
		OLED.PrintSmall("<", backX + 2, y2, backSelected);

		// NEXT button (>)
		bool nextSelected = (m_AnimSettingsOption == TAnimSettingsOption::Next);
		if (nextSelected)
			OLED.DrawFilledRect(nextX, y2 - 1, nextX + 12 - 1, y2 + 8, false);
		OLED.PrintSmall(">", nextX + 2, y2, nextSelected);

		// EXIT button (X)
		bool exitSelected = (m_AnimSettingsOption == TAnimSettingsOption::Exit);
		if (exitSelected)
			OLED.DrawFilledRect(exitX, y2 - 1, exitX + 12 - 1, y2 + 8, false);
		OLED.PrintSmall("X", exitX + 2, y2, exitSelected);
	}
	else
	{
		// Character LCD - simpler 2-line display
		char line1[21];
		char line2[21];

		switch (m_AnimSettingsOption)
		{
			case TAnimSettingsOption::Mode:
			{
				const char* modeStr = "Bar Graph";
				switch (m_VisualizationMode)
				{
					case TVisualizationMode::BarGraph:
						modeStr = "Bar Graph";
						break;
					case TVisualizationMode::Animation:
						modeStr = "Animation";
						break;
					case TVisualizationMode::Asteroids:
						modeStr = "Asteroids";
						break;
					case TVisualizationMode::MatrixRain:
						modeStr = "Matrix Rain";
						break;
					case TVisualizationMode::Oscilloscope:
						modeStr = "Oscilloscope";
						break;
					default:
						modeStr = "Unknown";
						break;
				}
				snprintf(line1, sizeof(line1), "Viz Mode");
				snprintf(line2, sizeof(line2), "%s", modeStr);
				break;
			}

			case TAnimSettingsOption::Next:
				snprintf(line1, sizeof(line1), "Next Page");
				snprintf(line2, sizeof(line2), "Preset menu");
				break;

			case TAnimSettingsOption::Back:
				snprintf(line1, sizeof(line1), "Back");
				snprintf(line2, sizeof(line2), "To main menu");
				break;

			case TAnimSettingsOption::Exit:
				snprintf(line1, sizeof(line1), "Exit Menu");
				snprintf(line2, sizeof(line2), "Press to exit");
				break;

			default:
				line1[0] = '\0';
				line2[0] = '\0';
				break;
		}

		LCD.Print(line1, 0, 0, true);
		LCD.Print(line2, 0, 1, true);
	}
}
// Preset menu navigation
void CMenu::NavigatePresetMenu(s8 nDelta)
{
	switch (m_PresetMenuScreen)
	{
		case TPresetMenuScreen::Main:
		{
			// Navigate main preset menu options
			s8 nOption = static_cast<s8>(m_PresetMenuOption) + nDelta;
			if (nOption < 0)
				nOption = static_cast<s8>(TPresetMenuOption::Count) - 1;
			else if (nOption >= static_cast<s8>(TPresetMenuOption::Count))
				nOption = 0;
			m_PresetMenuOption = static_cast<TPresetMenuOption>(nOption);
			break;
		}

		case TPresetMenuScreen::NameEditor:
		{
			// Navigate between character positions, SAVE, and CANCEL
			if (m_bEditing)
			{
				// Editing a character - adjust the character value
				u8 nCharIndex = static_cast<u8>(m_NameEditorOption);
				if (nCharIndex < 12)
				{
					char& ch = m_PresetNameBuffer[nCharIndex];

					// Character set: A-Z, 0-9, _ (letters first for easier access)
					if (nDelta > 0)
					{
						if (ch == ' ')
							ch = 'A';
						else if (ch >= 'A' && ch < 'Z')
							ch++;
						else if (ch == 'Z')
							ch = '0';
						else if (ch >= '0' && ch < '9')
							ch++;
						else if (ch == '9')
							ch = '_';
						else if (ch == '_')
							ch = 'A';
						else
							ch = 'A';
					}
					else if (nDelta < 0)
					{
						if (ch == ' ')
							ch = '_';
						else if (ch == 'A')
							ch = '_';
						else if (ch > 'A' && ch <= 'Z')
							ch--;
						else if (ch == '0')
							ch = 'Z';
						else if (ch > '0' && ch <= '9')
							ch--;
						else if (ch == '_')
							ch = '9';
						else
							ch = '_';
					}
				}
			}
			else
			{
				// Navigate between options (characters, SAVE, CANCEL)
				s8 nOption = static_cast<s8>(m_NameEditorOption) + nDelta;
				if (nOption < 0)
					nOption = static_cast<s8>(TNameEditorOption::Count) - 1;
				else if (nOption >= static_cast<s8>(TNameEditorOption::Count))
					nOption = 0;
				m_NameEditorOption = static_cast<TNameEditorOption>(nOption);
			}
			break;
		}

		case TPresetMenuScreen::PresetList:
		{
			// Navigate preset list
			s16 nNewSelection = m_nPresetListSelection + nDelta;
			if (nNewSelection < 0)
				nNewSelection = (m_nPresetCount > 0) ? m_nPresetCount - 1 : 0;
			else if (nNewSelection >= m_nPresetCount)
				nNewSelection = 0;
			m_nPresetListSelection = static_cast<u8>(nNewSelection);

			// Update scroll position to keep selection visible (2 items visible)
			if (m_nPresetListSelection < m_nPresetListScroll)
				m_nPresetListScroll = m_nPresetListSelection;
			else if (m_nPresetListSelection >= m_nPresetListScroll + 2)
				m_nPresetListScroll = m_nPresetListSelection - 1;
			break;
		}

		case TPresetMenuScreen::Confirm:
		{
			// Toggle Yes/No
			if (nDelta != 0)
				m_bConfirmYes = !m_bConfirmYes;
			break;
		}
	}
}

// Sanitize filename: convert spaces to underscores
void CMenu::SanitizeFileName(char* pDest, const char* pSrc, size_t nMaxLen)
{
	size_t i;
	for (i = 0; i < nMaxLen - 1 && pSrc[i] != '\0'; ++i)
	{
		if (pSrc[i] == ' ')
			pDest[i] = '_';
		else
			pDest[i] = pSrc[i];
	}
	pDest[i] = '\0';
}

// Check if preset exists
bool CMenu::PresetExists(const char* pName)
{
	char filename[32];
	SanitizeFileName(filename, pName, sizeof(filename));
	
	char path[64];
	snprintf(path, sizeof(path), "SD:/midi-presets/%s.mpr", filename);
	
	FIL file;
	FRESULT res = f_open(&file, path, FA_READ);
	if (res == FR_OK)
	{
		f_close(&file);
		return true;
	}
	return false;
}

// Save preset to file
bool CMenu::SavePreset(const char* pName)
{
	// Create directory if it doesn't exist
	f_mkdir("SD:/midi-presets");
	
	char filename[32];
	SanitizeFileName(filename, pName, sizeof(filename));
	
	char path[64];
	snprintf(path, sizeof(path), "SD:/midi-presets/%s.mpr", filename);
	
	MIDIPreset preset;
	strncpy(preset.Name, pName, 12);
	preset.Name[12] = '\0';
	preset.MutedChannels = m_nMutedChannels;
	memcpy(preset.ChannelRoute, m_ChannelRoute, sizeof(preset.ChannelRoute));
	memcpy(preset.ChannelVolume, m_ChannelVolume, sizeof(preset.ChannelVolume));
	memcpy(preset.ChannelProgram, m_ChannelProgram, sizeof(preset.ChannelProgram));
	
	FIL file;
	FRESULT res = f_open(&file, path, FA_WRITE | FA_CREATE_ALWAYS);
	if (res != FR_OK)
		return false;
	
	UINT bytesWritten;
	res = f_write(&file, &preset, sizeof(MIDIPreset), &bytesWritten);
	f_close(&file);
	
	return (res == FR_OK && bytesWritten == sizeof(MIDIPreset));
}

// Load preset from file
bool CMenu::LoadPreset(const char* pName)
{
	char filename[32];
	SanitizeFileName(filename, pName, sizeof(filename));
	
	char path[64];
	snprintf(path, sizeof(path), "SD:/midi-presets/%s.mpr", filename);
	
	FIL file;
	FRESULT res = f_open(&file, path, FA_READ);
	if (res != FR_OK)
		return false;
	
	MIDIPreset preset;
	UINT bytesRead;
	res = f_read(&file, &preset, sizeof(MIDIPreset), &bytesRead);
	f_close(&file);
	
	if (res != FR_OK || bytesRead != sizeof(MIDIPreset))
		return false;
	
	// Apply preset settings
	m_nMutedChannels = preset.MutedChannels;
	memcpy(m_ChannelRoute, preset.ChannelRoute, sizeof(m_ChannelRoute));
	memcpy(m_ChannelVolume, preset.ChannelVolume, sizeof(m_ChannelVolume));
	memcpy(m_ChannelProgram, preset.ChannelProgram, sizeof(m_ChannelProgram));

	// Signal that all programs need to be sent to the synth
	m_bSendAllPrograms = true;

	return true;
}

// Delete preset file
bool CMenu::DeletePreset(const char* pName)
{
	char filename[32];
	SanitizeFileName(filename, pName, sizeof(filename));
	
	char path[64];
	snprintf(path, sizeof(path), "SD:/midi-presets/%s.mpr", filename);
	
	FRESULT res = f_unlink(path);
	return (res == FR_OK);
}

// Load list of presets from directory
void CMenu::LoadPresetList()
{
	m_nPresetCount = 0;
	memset(m_PresetNames, 0, sizeof(m_PresetNames));
	
	DIR dir;
	FRESULT res = f_opendir(&dir, "SD:/midi-presets");
	if (res != FR_OK)
		return;
	
	FILINFO fno;
	while (m_nPresetCount < MaxPresets)
	{
		res = f_readdir(&dir, &fno);
		if (res != FR_OK || fno.fname[0] == 0)
			break;
		
		// Only process .mpr files
		size_t len = strlen(fno.fname);
		if (len > 4 && strcmp(fno.fname + len - 4, ".mpr") == 0)
		{
			// Load the preset to get its display name
			char path[64];
			snprintf(path, sizeof(path), "SD:/midi-presets/%s", fno.fname);
			
			FIL file;
			if (f_open(&file, path, FA_READ) == FR_OK)
			{
				MIDIPreset preset;
				UINT bytesRead;
				if (f_read(&file, &preset, sizeof(MIDIPreset), &bytesRead) == FR_OK && 
				    bytesRead == sizeof(MIDIPreset))
				{
					strncpy(m_PresetNames[m_nPresetCount], preset.Name, 12);
					m_PresetNames[m_nPresetCount][12] = '\0';
					m_nPresetCount++;
				}
				f_close(&file);
			}
		}
	}
	
	f_closedir(&dir);

	// Sort preset names alphabetically using bubble sort
	if (m_nPresetCount > 1)
	{
		for (u8 i = 0; i < m_nPresetCount - 1; ++i)
		{
			for (u8 j = 0; j < m_nPresetCount - i - 1; ++j)
			{
				if (strcmp(m_PresetNames[j], m_PresetNames[j + 1]) > 0)
				{
					// Swap
					char temp[13];
					strcpy(temp, m_PresetNames[j]);
					strcpy(m_PresetNames[j], m_PresetNames[j + 1]);
					strcpy(m_PresetNames[j + 1], temp);
				}
			}
		}
	}
}

// Initialize default preset
void CMenu::InitializeDefaultPreset()
{
	// Check if default preset exists
	if (!PresetExists("DEFAULT"))
	{
		// Create default preset with standard settings
		MIDIPreset preset;
		strncpy(preset.Name, "DEFAULT", 12);
		preset.Name[12] = '\0';
		preset.MutedChannels = 0;  // No channels muted

		for (u8 i = 0; i < 16; ++i)
		{
			preset.ChannelRoute[i] = i;  // No routing
			preset.ChannelVolume[i] = DefaultChannelVolume;
			preset.ChannelProgram[i] = 0;  // Default program 0
		}
		
		// Save to file
		f_mkdir("SD:/midi-presets");
		
		FIL file;
		if (f_open(&file, "SD:/midi-presets/DEFAULT.mpr", FA_WRITE | FA_CREATE_ALWAYS) == FR_OK)
		{
			UINT bytesWritten;
			f_write(&file, &preset, sizeof(MIDIPreset), &bytesWritten);
			f_close(&file);
		}
	}
}

// Draw preset menu - dispatcher
void CMenu::DrawPresetMenu(CLCD& LCD) const
{
	switch (m_PresetMenuScreen)
	{
		case TPresetMenuScreen::Main:
			DrawPresetMainMenu(LCD);
			break;
		
		case TPresetMenuScreen::NameEditor:
			DrawNameEditor(LCD);
			break;
		
		case TPresetMenuScreen::PresetList:
			DrawPresetList(LCD);
			break;
		
		case TPresetMenuScreen::Confirm:
			DrawConfirmation(LCD);
			break;
	}
}

// Draw preset main menu
void CMenu::DrawPresetMainMenu(CLCD& LCD) const
{
	const bool bGraphical = (LCD.GetType() == CLCD::TType::Graphical);
	
	if (bGraphical)
	{
		// OLED Display - preset menu main screen
		// Row 0 (y=0):  "PRESET MENU" title
		// Row 1 (y=11): SAVE + LOAD + DEL buttons
		// Row 2 (y=21): BACK + EXIT buttons

		CSSD1306& OLED = static_cast<CSSD1306&>(LCD);

		const int16_t y0 = 0;
		const int16_t y1 = 11;
		const int16_t y2 = 21;

		// Row 0: Title
		OLED.PrintSmall("PRESET MENU", 0, y0, false);

		// Row 1: SAVE button
		bool saveSelected = (m_PresetMenuOption == TPresetMenuOption::Save);
		if (saveSelected)
			OLED.DrawFilledRect(0, y1 - 1, 24 - 1, y1 + 8, false);
		OLED.PrintSmall("SAVE", 1, y1, saveSelected);

		// LOAD button
		bool loadSelected = (m_PresetMenuOption == TPresetMenuOption::Load);
		if (loadSelected)
			OLED.DrawFilledRect(30, y1 - 1, 30 + 24 - 1, y1 + 8, false);
		OLED.PrintSmall("LOAD", 31, y1, loadSelected);

		// DEL button (shortened from DELETE)
		bool deleteSelected = (m_PresetMenuOption == TPresetMenuOption::Delete);
		if (deleteSelected)
			OLED.DrawFilledRect(60, y1 - 1, 60 + 18 - 1, y1 + 8, false);
		OLED.PrintSmall("DEL", 61, y1, deleteSelected);

		// Row 2: BACK button (<)
		bool backSelected = (m_PresetMenuOption == TPresetMenuOption::Back);
		if (backSelected)
			OLED.DrawFilledRect(90, y2 - 1, 90 + 12 - 1, y2 + 8, false);
		OLED.PrintSmall("<", 92, y2, backSelected);

		// EXIT button (X)
		bool exitSelected = (m_PresetMenuOption == TPresetMenuOption::Exit);
		if (exitSelected)
			OLED.DrawFilledRect(108, y2 - 1, 108 + 12 - 1, y2 + 8, false);
		OLED.PrintSmall("X", 110, y2, exitSelected);
	}
	else
	{
		// Character LCD - simpler 2-line display
		char line1[21];
		char line2[21];
		
		switch (m_PresetMenuOption)
		{
			case TPresetMenuOption::Save:
				snprintf(line1, sizeof(line1), "Save Preset");
				snprintf(line2, sizeof(line2), "Press to save");
				break;
			
			case TPresetMenuOption::Load:
				snprintf(line1, sizeof(line1), "Load Preset");
				snprintf(line2, sizeof(line2), "Press to load");
				break;
			
			case TPresetMenuOption::Delete:
				snprintf(line1, sizeof(line1), "Delete Preset");
				snprintf(line2, sizeof(line2), "Press to delete");
				break;
			
			case TPresetMenuOption::Back:
				snprintf(line1, sizeof(line1), "Back");
				snprintf(line2, sizeof(line2), "To anim settings");
				break;
			
			case TPresetMenuOption::Exit:
				snprintf(line1, sizeof(line1), "Exit Menu");
				snprintf(line2, sizeof(line2), "Press to exit");
				break;
			
			default:
				line1[0] = '\0';
				line2[0] = '\0';
				break;
		}
		
		LCD.Print(line1, 0, 0, true);
		LCD.Print(line2, 0, 1, true);
	}
}

// Draw name editor screen
void CMenu::DrawNameEditor(CLCD& LCD) const
{
	const bool bGraphical = (LCD.GetType() == CLCD::TType::Graphical);
	
	if (bGraphical)
	{
		// OLED Display - name editor
		// Row 0 (y=0):  "SAVE AS:" title
		// Row 1 (y=11): Name with cursor indicator
		// Row 2 (y=21): SAVE + CANCEL buttons
		
		CSSD1306& OLED = static_cast<CSSD1306&>(LCD);
		
		const int16_t y0 = 0;
		const int16_t y1 = 11;
		const int16_t y2 = 21;
		
		// Row 0: Title
		OLED.PrintSmall("SAVE AS:", 0, y0, false);
		
		// Row 1: Display name with selection indicators
		for (u8 i = 0; i < 12; ++i)
		{
			char ch[2] = { m_PresetNameBuffer[i], '\0' };
			TNameEditorOption charOption = static_cast<TNameEditorOption>(i);
			bool isSelected = (m_NameEditorOption == charOption);

			// Draw character
			if (m_bEditing && isSelected)
			{
				// Highlight editing character
				OLED.DrawFilledRect(i * 6, y1, i * 6 + 5, y1 + 7, false);
				OLED.PrintSmall(ch, i * 6, y1, true);
			}
			else
			{
				OLED.PrintSmall(ch, i * 6, y1, false);

				// Draw underscore cursor below selected character
				if (isSelected && !m_bEditing)
					OLED.DrawFilledRect(i * 6, y1 + 8, i * 6 + 5, y1 + 8, false);
			}
		}

		// Row 2: SAVE and CANCEL buttons
		bool saveSelected = (m_NameEditorOption == TNameEditorOption::Save);
		if (saveSelected)
			OLED.DrawFilledRect(0, y2 - 1, 24 - 1, y2 + 8, false);
		OLED.PrintSmall("SAVE", 1, y2, saveSelected);

		bool cancelSelected = (m_NameEditorOption == TNameEditorOption::Cancel);
		if (cancelSelected)
			OLED.DrawFilledRect(30, y2 - 1, 30 + 36 - 1, y2 + 8, false);
		OLED.PrintSmall("CANCEL", 31, y2, cancelSelected);
	}
	else
	{
		// Character LCD
		char line1[21];
		char line2[21];

		snprintf(line1, sizeof(line1), "Name: %.12s", m_PresetNameBuffer);

		if (m_NameEditorOption == TNameEditorOption::Save)
			snprintf(line2, sizeof(line2), "> SAVE");
		else if (m_NameEditorOption == TNameEditorOption::Cancel)
			snprintf(line2, sizeof(line2), "> CANCEL");
		else
		{
			u8 nPos = static_cast<u8>(m_NameEditorOption);
			if (m_bEditing)
				snprintf(line2, sizeof(line2), "Editing pos %d", nPos + 1);
			else
				snprintf(line2, sizeof(line2), "At pos %d", nPos + 1);
		}

		LCD.Print(line1, 0, 0, true);
		LCD.Print(line2, 0, 1, true);
	}
}

// Draw preset list screen
void CMenu::DrawPresetList(CLCD& LCD) const
{
	const bool bGraphical = (LCD.GetType() == CLCD::TType::Graphical);
	
	if (bGraphical)
	{
		// OLED Display - preset list
		// Row 0 (y=0):  "SELECT PRESET:" or "DELETE PRESET:" title
		// Row 1 (y=11): First visible preset
		// Row 2 (y=21): Second visible preset
		
		CSSD1306& OLED = static_cast<CSSD1306&>(LCD);
		
		const int16_t y0 = 0;
		const int16_t y1 = 11;
		const int16_t y2 = 21;
		
		// Row 0: Title
		const char* title = (m_PresetListOperation == TPresetListOperation::Load) 
			? "SELECT PRESET:" : "DELETE PRESET:";
		OLED.PrintSmall(title, 0, y0, false);
		
		// Show 2 presets at a time
		for (u8 i = 0; i < 2; ++i)
		{
			u8 presetIndex = m_nPresetListScroll + i;
			if (presetIndex >= m_nPresetCount)
				break;
			
			bool isSelected = (presetIndex == m_nPresetListSelection);
			int16_t y = (i == 0) ? y1 : y2;
			
			if (isSelected)
				OLED.DrawFilledRect(0, y - 1, 127, y + 8, false);
			
			OLED.PrintSmall(m_PresetNames[presetIndex], 2, y, isSelected);
		}
		
		// Draw scroll indicator if there are more presets
		if (m_nPresetCount > 2)
		{
			// Show which presets are visible (e.g., "1-2/5")
			char scrollInfo[16];
			snprintf(scrollInfo, sizeof(scrollInfo), "%d-%d/%d", 
				m_nPresetListScroll + 1, 
				(m_nPresetListScroll + 2 <= m_nPresetCount) ? m_nPresetListScroll + 2 : m_nPresetCount,
				m_nPresetCount);
			OLED.PrintSmall(scrollInfo, 108, y2, false);
		}
	}
	else
	{
		// Character LCD
		char line1[21];
		char line2[21];
		
		if (m_nPresetCount == 0)
		{
			snprintf(line1, sizeof(line1), "No presets");
			line2[0] = '\0';
		}
		else
		{
			snprintf(line1, sizeof(line1), "> %.12s", m_PresetNames[m_nPresetListSelection]);
			snprintf(line2, sizeof(line2), "%d/%d", m_nPresetListSelection + 1, m_nPresetCount);
		}
		
		LCD.Print(line1, 0, 0, true);
		LCD.Print(line2, 0, 1, true);
	}
}

// Draw confirmation dialog
void CMenu::DrawConfirmation(CLCD& LCD) const
{
	const bool bGraphical = (LCD.GetType() == CLCD::TType::Graphical);
	
	if (bGraphical)
	{
		// OLED Display - confirmation dialog
		// Row 0 (y=0):  "Overwrite" or "Delete"
		// Row 1 (y=11): Preset name
		// Row 2 (y=21): YES + NO buttons
		
		CSSD1306& OLED = static_cast<CSSD1306&>(LCD);
		
		const int16_t y0 = 0;
		const int16_t y1 = 11;
		const int16_t y2 = 21;

		// Determine message based on context
		const char* actionText = (m_PresetListOperation == TPresetListOperation::Delete) ? "Delete" : "Overwrite";
		OLED.PrintSmall(actionText, 0, y0, false);

		// Show preset name being acted upon
		if (m_nPresetListSelection < m_nPresetCount)
			OLED.PrintSmall(m_PresetNames[m_nPresetListSelection], 0, y1, false);
		
		// Row 2: YES button
		bool yesSelected = m_bConfirmYes;
		if (yesSelected)
			OLED.DrawFilledRect(30, y2 - 1, 30 + 18 - 1, y2 + 8, false);
		OLED.PrintSmall("YES", 31, y2, yesSelected);
		
		// NO button
		bool noSelected = !m_bConfirmYes;
		if (noSelected)
			OLED.DrawFilledRect(60, y2 - 1, 60 + 12 - 1, y2 + 8, false);
		OLED.PrintSmall("NO", 61, y2, noSelected);
	}
	else
	{
		// Character LCD
		char line1[21];
		char line2[21];
		
		snprintf(line1, sizeof(line1), "Confirm?");
		snprintf(line2, sizeof(line2), m_bConfirmYes ? "> YES   NO" : "  YES > NO");
		
		LCD.Print(line1, 0, 0, true);
		LCD.Print(line2, 0, 1, true);
	}
}

