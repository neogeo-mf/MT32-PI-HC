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
#include "config.h"
#include <circle/timer.h>
#include <circle/logger.h>
#include <fatfs/ff.h>
#include <cstdio>
#include <cstring>
#include <algorithm>

LOGMODULE("menu");

CMenu::CMenu()
	: m_bActive(false),
	  m_bEditing(false),
	  m_bShowAnimSettings(false),
	  m_bShowFXSettings(false),
	  m_bShowReverbSettings(false),
	  m_bShowChorusSettings(false),
	  m_bShowPresetMenu(false),
	  m_nSelectedChannel(0),
	  m_SelectedOption(TMenuOption::Channel),
	  m_FXSettingsOption(TFXSettingsOption::ReverbSettings),
	  m_ReverbSettingsOption(TReverbSettingsOption::RoomSize),
	  m_ChorusSettingsOption(TChorusSettingsOption::Depth),
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
	  m_bSendAllPrograms(false),
	  m_bPendingFXChange(false),
	  m_nFXChangeChannel(0),
	  m_nFXChangeCC(0),
	  m_nFXChangeValue(0),
	  m_bSendAllFX(false),
	  m_nReverbRoomSize(20),
	  m_nReverbDamping(0),
	  m_nReverbWidth(50),
	  m_nReverbLevel(90),
	  m_nChorusDepth(80),
	  m_nChorusSpeed(3),
	  m_nChorusLevel(20),
	  m_nChorusVoices(3),
	  m_bPendingGlobalFXChange(false),
	  m_PendingGlobalFXParameter(TGlobalFXParameter::ReverbRoomSize),
	  m_nPendingGlobalFXValue(0),
	  m_bSendAllGlobalFX(false)
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

		// Initialize FX settings to GM defaults
		m_ChannelReverbSend[i] = 40;   // GM default reverb
		m_ChannelChorusSend[i] = 0;    // No chorus by default
		m_ChannelPan[i] = 64;          // Center pan
		m_ChannelExpression[i] = 127;  // Full expression
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
	m_bShowFXSettings = false;
	m_bShowReverbSettings = false;
	m_bShowChorusSettings = false;
	m_bShowPresetMenu = false;
	m_SelectedOption = TMenuOption::Channel;
	m_FXSettingsOption = TFXSettingsOption::ReverbSettings;
	m_AnimSettingsOption = TAnimSettingsOption::Mode;
	m_PresetMenuOption = TPresetMenuOption::Save;
	m_PresetMenuScreen = TPresetMenuScreen::Main;
}

void CMenu::ExitMenu()
{
	m_bActive = false;
	m_bEditing = false;
	m_bShowAnimSettings = false;
	m_bShowFXSettings = false;
	m_bShowReverbSettings = false;
	m_bShowChorusSettings = false;
	m_bShowPresetMenu = false;
}

void CMenu::LoadVisualizationMode()
{
	// Try to load from state file first
	FIL File;
	FRESULT res = f_open(&File, "SD:/visualization_state.cfg", FA_READ);
	if (res == FR_OK)
	{
		char buffer[64];
		UINT bytesRead;
		if (f_read(&File, buffer, sizeof(buffer) - 1, &bytesRead) == FR_OK && bytesRead > 0)
		{
			buffer[bytesRead] = '\0';

			// Trim newline
			for (size_t i = 0; i < sizeof(buffer); ++i)
			{
				if (buffer[i] == '\n' || buffer[i] == '\r')
				{
					buffer[i] = '\0';
					break;
				}
			}

			// Parse the mode string
			if (strcmp(buffer, "bargraph") == 0)
				m_VisualizationMode = TVisualizationMode::BarGraph;
			else if (strcmp(buffer, "animation") == 0)
				m_VisualizationMode = TVisualizationMode::Animation;
			else if (strcmp(buffer, "asteroids") == 0)
				m_VisualizationMode = TVisualizationMode::Asteroids;
			else if (strcmp(buffer, "matrixrain") == 0)
				m_VisualizationMode = TVisualizationMode::MatrixRain;
			else if (strcmp(buffer, "oscilloscope") == 0)
				m_VisualizationMode = TVisualizationMode::Oscilloscope;

			LOGNOTE("Loaded visualization mode: %s", buffer);
		}

		f_close(&File);
	}
	else
	{
		LOGWARN("Visualization state file not found, f_open returned %d", res);
	}
}

void CMenu::SaveVisualizationMode()
{
	// Save to a separate state file to avoid overwriting main config
	FIL File;
	FRESULT res = f_open(&File, "SD:/visualization_state.cfg", FA_WRITE | FA_CREATE_ALWAYS);
	if (res != FR_OK)
	{
		LOGWARN("Failed to save visualization mode, f_open returned %d", res);
		return;
	}

	const char* modeStr = "bargraph";
	switch (m_VisualizationMode)
	{
		case TVisualizationMode::BarGraph: modeStr = "bargraph"; break;
		case TVisualizationMode::Animation: modeStr = "animation"; break;
		case TVisualizationMode::Asteroids: modeStr = "asteroids"; break;
		case TVisualizationMode::MatrixRain: modeStr = "matrixrain"; break;
		case TVisualizationMode::Oscilloscope: modeStr = "oscilloscope"; break;
		case TVisualizationMode::Count: break;  // Not a valid mode to save
	}

	char buffer[64];
	snprintf(buffer, sizeof(buffer), "%s\n", modeStr);

	UINT bytesWritten;
	res = f_write(&File, buffer, strlen(buffer), &bytesWritten);
	if (res == FR_OK)
		res = f_sync(&File);  // Ensure data is flushed to disk
	f_close(&File);

	if (res == FR_OK)
		LOGNOTE("Saved visualization mode: %s", modeStr);
	else
		LOGWARN("Failed to write visualization mode, f_write returned %d", res);
}

void CMenu::Navigate(s8 nDelta)
{
	if (m_bShowPresetMenu)
	{
		NavigatePresetMenu(nDelta);
	}
	else if (m_bShowReverbSettings)
	{
		if (m_bEditing)
			AdjustReverbSettingsValue(nDelta);
		else
			NavigateReverbSettings(nDelta);
	}
	else if (m_bShowChorusSettings)
	{
		if (m_bEditing)
			AdjustChorusSettingsValue(nDelta);
		else
			NavigateChorusSettings(nDelta);
	}
	else if (m_bShowAnimSettings)
	{
		if (m_bEditing)
			AdjustAnimSettingsValue(nDelta);
		else
			NavigateAnimSettings(nDelta);
	}
	else if (m_bShowFXSettings)
	{
		if (m_bEditing)
			AdjustFXSettingsValue(nDelta);
		else
			NavigateFXSettings(nDelta);
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
					// Go back to animation settings - smart selection: default to Next (where they came from)
					m_bShowPresetMenu = false;
					m_bShowAnimSettings = true;
					m_bEditing = false;
					m_AnimSettingsOption = TAnimSettingsOption::Next;
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
				// Check if Back or Exit was selected
				if (m_nPresetListSelection == m_nPresetCount)
				{
					// Back option selected - return to main preset menu
					m_PresetMenuScreen = TPresetMenuScreen::Main;
				}
				else if (m_nPresetListSelection == m_nPresetCount + 1)
				{
					// Exit option selected - exit menu entirely
					ExitMenu();
				}
				else if (m_nPresetListSelection < m_nPresetCount)
				{
					// Preset selected
					if (m_PresetListOperation == TPresetListOperation::Load)
					{
						// Load selected preset
						if (LoadPreset(m_PresetNames[m_nPresetListSelection]))
						{
							// Success - exit menu to show changes
							ExitMenu();
						}
						// else: Show error (TODO: add error display)
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
	else if (m_bShowReverbSettings)
	{
		// Handle reverb settings screen
		if (m_ReverbSettingsOption == TReverbSettingsOption::Back)
		{
			// Go back to FX settings - smart selection: default to ReverbSettings button
			m_bShowReverbSettings = false;
			m_bEditing = false;
			m_FXSettingsOption = TFXSettingsOption::ReverbSettings;
		}
		else if (m_ReverbSettingsOption == TReverbSettingsOption::Exit)
		{
			// Exit menu
			ExitMenu();
		}
		else
		{
			// Toggle edit mode for current option
			m_EditToggleHistory[m_nEditToggleIndex] = nCurrentTime;
			m_nEditToggleIndex = (m_nEditToggleIndex + 1) % 5;
			m_nLastEditToggleTime = nCurrentTime;
			m_bEditing = !m_bEditing;
		}
	}
	else if (m_bShowChorusSettings)
	{
		// Handle chorus settings screen
		if (m_ChorusSettingsOption == TChorusSettingsOption::Back)
		{
			// Go back to FX settings - smart selection: default to ChorusSettings button
			m_bShowChorusSettings = false;
			m_bEditing = false;
			m_FXSettingsOption = TFXSettingsOption::ChorusSettings;
		}
		else if (m_ChorusSettingsOption == TChorusSettingsOption::Exit)
		{
			// Exit menu
			ExitMenu();
		}
		else
		{
			// Toggle edit mode for current option
			m_EditToggleHistory[m_nEditToggleIndex] = nCurrentTime;
			m_nEditToggleIndex = (m_nEditToggleIndex + 1) % 5;
			m_nLastEditToggleTime = nCurrentTime;
			m_bEditing = !m_bEditing;
		}
	}
	else if (m_bShowFXSettings)
	{
		// Handle FX settings screen
		if (m_FXSettingsOption == TFXSettingsOption::ReverbSettings)
		{
			// Go to reverb settings - smart selection: default to Back (to return easily)
			m_bShowReverbSettings = true;
			m_bEditing = false;
			m_ReverbSettingsOption = TReverbSettingsOption::Back;
		}
		else if (m_FXSettingsOption == TFXSettingsOption::ChorusSettings)
		{
			// Go to chorus settings - smart selection: default to Back (to return easily)
			m_bShowChorusSettings = true;
			m_bEditing = false;
			m_ChorusSettingsOption = TChorusSettingsOption::Back;
		}
		else if (m_FXSettingsOption == TFXSettingsOption::Next)
		{
			// Go to animation settings - smart selection: default to Next (forward momentum)
			m_bShowFXSettings = false;
			m_bShowAnimSettings = true;
			m_bEditing = false;
			m_AnimSettingsOption = TAnimSettingsOption::Next;
		}
		else if (m_FXSettingsOption == TFXSettingsOption::Back)
		{
			// Go back to main menu - smart selection: default to Next (where they came from)
			m_bShowFXSettings = false;
			m_bEditing = false;
			m_SelectedOption = TMenuOption::Next;
		}
		else if (m_FXSettingsOption == TFXSettingsOption::Exit)
		{
			// Exit menu
			ExitMenu();
		}
		else
		{
			// Toggle edit mode for current option
			m_EditToggleHistory[m_nEditToggleIndex] = nCurrentTime;
			m_nEditToggleIndex = (m_nEditToggleIndex + 1) % 5;
			m_nLastEditToggleTime = nCurrentTime;
			m_bEditing = !m_bEditing;
		}
	}
	else if (m_bShowAnimSettings)
	{
		// Handle animation settings screen
		if (m_AnimSettingsOption == TAnimSettingsOption::Next)
		{
			// Go to preset menu - smart selection: default to Back (no further Next available)
			m_bShowAnimSettings = false;
			m_bShowPresetMenu = true;
			m_bEditing = false;
			m_PresetMenuScreen = TPresetMenuScreen::Main;
			m_PresetMenuOption = TPresetMenuOption::Back;
			LoadPresetList();
		}
		else if (m_AnimSettingsOption == TAnimSettingsOption::Back)
		{
			// Go back to FX settings - smart selection: default to Next (where they came from)
			m_bShowAnimSettings = false;
			m_bShowFXSettings = true;
			m_bEditing = false;
			m_FXSettingsOption = TFXSettingsOption::Next;
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
		// Go to FX settings screen - smart selection: default to Next (forward momentum)
		m_bShowFXSettings = true;
		m_bEditing = false;
		m_FXSettingsOption = TFXSettingsOption::Next;
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

		// Save the new mode to persistent storage
		SaveVisualizationMode();
	}
}

void CMenu::Draw(CLCD& LCD) const
{
	if (!m_bActive)
		return;

	if (m_bShowPresetMenu)
		DrawPresetMenu(LCD);
	else if (m_bShowReverbSettings)
		DrawReverbSettings(LCD);
	else if (m_bShowChorusSettings)
		DrawChorusSettings(LCD);
	else if (m_bShowAnimSettings)
		DrawAnimSettings(LCD);
	else if (m_bShowFXSettings)
		DrawFXSettings(LCD);
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
				snprintf(line2, sizeof(line2), "FX settings");
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
			// Navigate preset list + Back/Exit options (2 extra entries)
			u8 nTotalEntries = m_nPresetCount + 2;  // Presets + Back + Exit
			s16 nNewSelection = m_nPresetListSelection + nDelta;
			if (nNewSelection < 0)
				nNewSelection = (nTotalEntries > 0) ? nTotalEntries - 1 : 0;
			else if (nNewSelection >= nTotalEntries)
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

	// Create preset file structure with version header
	struct {
		u8 Version;
		MIDIPreset Preset;
	} presetFile;

	presetFile.Version = 3;  // Version 3 format (includes per-channel FX and global FX parameters)

	// Copy preset data
	strncpy(presetFile.Preset.Name, pName, 12);
	presetFile.Preset.Name[12] = '\0';
	presetFile.Preset.MutedChannels = m_nMutedChannels;
	memcpy(presetFile.Preset.ChannelRoute, m_ChannelRoute, sizeof(presetFile.Preset.ChannelRoute));
	memcpy(presetFile.Preset.ChannelVolume, m_ChannelVolume, sizeof(presetFile.Preset.ChannelVolume));
	memcpy(presetFile.Preset.ChannelProgram, m_ChannelProgram, sizeof(presetFile.Preset.ChannelProgram));

	// Copy per-channel FX settings (Version 2+)
	memcpy(presetFile.Preset.ChannelReverbSend, m_ChannelReverbSend, sizeof(presetFile.Preset.ChannelReverbSend));
	memcpy(presetFile.Preset.ChannelChorusSend, m_ChannelChorusSend, sizeof(presetFile.Preset.ChannelChorusSend));
	memcpy(presetFile.Preset.ChannelPan, m_ChannelPan, sizeof(presetFile.Preset.ChannelPan));
	memcpy(presetFile.Preset.ChannelExpression, m_ChannelExpression, sizeof(presetFile.Preset.ChannelExpression));

	// Copy global FX parameters (new in Version 3)
	presetFile.Preset.ReverbRoomSize = m_nReverbRoomSize;
	presetFile.Preset.ReverbDamping = m_nReverbDamping;
	presetFile.Preset.ReverbWidth = m_nReverbWidth;
	presetFile.Preset.ReverbLevel = m_nReverbLevel;
	presetFile.Preset.ChorusDepth = m_nChorusDepth;
	presetFile.Preset.ChorusSpeed = m_nChorusSpeed;
	presetFile.Preset.ChorusLevel = m_nChorusLevel;
	presetFile.Preset.ChorusVoices = m_nChorusVoices;

	FIL file;
	FRESULT res = f_open(&file, path, FA_WRITE | FA_CREATE_ALWAYS);
	if (res != FR_OK)
		return false;

	UINT bytesWritten;
	res = f_write(&file, &presetFile, sizeof(presetFile), &bytesWritten);
	if (res == FR_OK)
		res = f_sync(&file);
	f_close(&file);

	LOGNOTE("Saved preset '%s' (Version 3, %u bytes)", pName, (unsigned int)bytesWritten);

	return (res == FR_OK && bytesWritten == sizeof(presetFile));
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

	// Get file size to determine version
	DWORD fileSize = f_size(&file);

	// Calculate expected sizes for each version
	const DWORD Version1Size = 64;  // Old format (no version byte, no FX): Name[13] + u16 + 3*u8[16] = 64 bytes
	const DWORD Version2Size = 130;  // Version 2: 1 byte version + 1 padding + base (64) + per-channel FX (64) = 130 bytes
	const DWORD Version3Size = 138;  // Version 3: 1 byte version + 1 padding + full preset (136) = 138 bytes (struct padding!)

	if (fileSize == Version1Size)
	{
		// Version 1 format (old preset without FX settings)
		LOGNOTE("Loading Version 1 preset '%s'", pName);

		// Read old-format preset (no version byte, 64 bytes)
		struct {
			char Name[13];
			u16 MutedChannels;
			u8 ChannelRoute[16];
			u8 ChannelVolume[16];
			u8 ChannelProgram[16];
		} oldPreset;

		UINT bytesRead;
		res = f_read(&file, &oldPreset, sizeof(oldPreset), &bytesRead);
		f_close(&file);

		if (res != FR_OK || bytesRead != sizeof(oldPreset))
			return false;

		// Apply old settings
		m_nMutedChannels = oldPreset.MutedChannels;
		memcpy(m_ChannelRoute, oldPreset.ChannelRoute, sizeof(m_ChannelRoute));
		memcpy(m_ChannelVolume, oldPreset.ChannelVolume, sizeof(m_ChannelVolume));
		memcpy(m_ChannelProgram, oldPreset.ChannelProgram, sizeof(m_ChannelProgram));

		// Initialize per-channel FX settings to defaults (GM standard)
		for (u8 i = 0; i < MIDIChannelCount; ++i)
		{
			m_ChannelReverbSend[i] = 40;   // GM default reverb
			m_ChannelChorusSend[i] = 0;    // No chorus by default
			m_ChannelPan[i] = 64;          // Center pan
			m_ChannelExpression[i] = 127;  // Full expression
		}

		// Global FX parameters remain unchanged (keep current values)

		// Signal that all programs and FX need to be sent to the synth
		m_bSendAllPrograms = true;
		m_bSendAllFX = true;

		LOGNOTE("Loaded Version 1 preset, initialized per-channel FX to defaults");
		return true;
	}
	else if (fileSize == Version2Size)
	{
		// Version 2 format (with version byte and per-channel FX settings, no global FX)
		LOGNOTE("Loading Version 2 preset '%s'", pName);

		// Define Version 2 preset structure (without global FX parameters)
		struct {
			char Name[13];
			u16 MutedChannels;
			u8 ChannelRoute[16];
			u8 ChannelVolume[16];
			u8 ChannelProgram[16];
			u8 ChannelReverbSend[16];
			u8 ChannelChorusSend[16];
			u8 ChannelPan[16];
			u8 ChannelExpression[16];
		} v2Preset;

		struct {
			u8 Version;
			decltype(v2Preset) Preset;
		} presetFile;

		UINT bytesRead;
		res = f_read(&file, &presetFile, sizeof(presetFile), &bytesRead);
		f_close(&file);

		if (res != FR_OK || bytesRead != sizeof(presetFile))
			return false;

		if (presetFile.Version != 2)
		{
			LOGWARN("Unexpected version %u in Version 2 file", presetFile.Version);
			return false;
		}

		// Apply all settings including per-channel FX
		m_nMutedChannels = presetFile.Preset.MutedChannels;
		memcpy(m_ChannelRoute, presetFile.Preset.ChannelRoute, sizeof(m_ChannelRoute));
		memcpy(m_ChannelVolume, presetFile.Preset.ChannelVolume, sizeof(m_ChannelVolume));
		memcpy(m_ChannelProgram, presetFile.Preset.ChannelProgram, sizeof(m_ChannelProgram));
		memcpy(m_ChannelReverbSend, presetFile.Preset.ChannelReverbSend, sizeof(m_ChannelReverbSend));
		memcpy(m_ChannelChorusSend, presetFile.Preset.ChannelChorusSend, sizeof(m_ChannelChorusSend));
		memcpy(m_ChannelPan, presetFile.Preset.ChannelPan, sizeof(m_ChannelPan));
		memcpy(m_ChannelExpression, presetFile.Preset.ChannelExpression, sizeof(m_ChannelExpression));

		// Global FX parameters remain unchanged (keep current values)

		// Signal that all programs and FX need to be sent to the synth
		m_bSendAllPrograms = true;
		m_bSendAllFX = true;

		LOGNOTE("Loaded Version 2 preset with per-channel FX");
		return true;
	}
	else if (fileSize == Version3Size)
	{
		// Version 3 format (with version byte, per-channel FX, and global FX parameters)
		LOGNOTE("Loading Version 3 preset '%s'", pName);

		struct {
			u8 Version;
			MIDIPreset Preset;
		} presetFile;

		UINT bytesRead;
		res = f_read(&file, &presetFile, sizeof(presetFile), &bytesRead);
		f_close(&file);

		if (res != FR_OK || bytesRead != sizeof(presetFile))
			return false;

		if (presetFile.Version != 3)
		{
			LOGWARN("Unknown preset version %u", presetFile.Version);
			return false;
		}

		// Apply all settings including per-channel and global FX
		m_nMutedChannels = presetFile.Preset.MutedChannels;
		memcpy(m_ChannelRoute, presetFile.Preset.ChannelRoute, sizeof(m_ChannelRoute));
		memcpy(m_ChannelVolume, presetFile.Preset.ChannelVolume, sizeof(m_ChannelVolume));
		memcpy(m_ChannelProgram, presetFile.Preset.ChannelProgram, sizeof(m_ChannelProgram));
		memcpy(m_ChannelReverbSend, presetFile.Preset.ChannelReverbSend, sizeof(m_ChannelReverbSend));
		memcpy(m_ChannelChorusSend, presetFile.Preset.ChannelChorusSend, sizeof(m_ChannelChorusSend));
		memcpy(m_ChannelPan, presetFile.Preset.ChannelPan, sizeof(m_ChannelPan));
		memcpy(m_ChannelExpression, presetFile.Preset.ChannelExpression, sizeof(m_ChannelExpression));

		// Load global FX parameters (new in Version 3)
		m_nReverbRoomSize = presetFile.Preset.ReverbRoomSize;
		m_nReverbDamping = presetFile.Preset.ReverbDamping;
		m_nReverbWidth = presetFile.Preset.ReverbWidth;
		m_nReverbLevel = presetFile.Preset.ReverbLevel;
		m_nChorusDepth = presetFile.Preset.ChorusDepth;
		m_nChorusSpeed = presetFile.Preset.ChorusSpeed;
		m_nChorusLevel = presetFile.Preset.ChorusLevel;
		m_nChorusVoices = presetFile.Preset.ChorusVoices;

		// Signal that all programs, per-channel FX, and global FX need to be sent to the synth
		m_bSendAllPrograms = true;
		m_bSendAllFX = true;
		m_bSendAllGlobalFX = true;  // Trigger all global FX parameters to be sent

		LOGNOTE("Loaded Version 3 preset with per-channel and global FX");
		return true;
	}
	else
	{
		// Unknown file size
		f_close(&file);
		LOGWARN("Invalid preset file size: %u bytes (expected %u, %u, or %u)",
		        (unsigned int)fileSize, (unsigned int)Version1Size, (unsigned int)Version2Size, (unsigned int)Version3Size);
		return false;
	}
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
				DWORD fileSize = f_size(&file);
				char presetName[13] = {0};
				bool bNameLoaded = false;

				// Detect version by file size and read name accordingly
				if (fileSize == 64)  // Version 1
				{
					// Name is at the start
					UINT bytesRead;
					if (f_read(&file, presetName, 13, &bytesRead) == FR_OK && bytesRead == 13)
						bNameLoaded = true;
				}
				else if (fileSize == 130)  // Version 2
				{
					// Skip version header (1 byte version + 1 byte padding = 2 bytes), then read name
					u8 header[2];
					UINT bytesRead;
					if (f_read(&file, header, 2, &bytesRead) == FR_OK && bytesRead == 2 &&
					    f_read(&file, presetName, 13, &bytesRead) == FR_OK && bytesRead == 13)
						bNameLoaded = true;
				}
				else if (fileSize == 138)  // Version 3
				{
					// Skip version header (1 byte version + 1 byte padding = 2 bytes), then read name
					u8 header[2];
					UINT bytesRead;
					if (f_read(&file, header, 2, &bytesRead) == FR_OK && bytesRead == 2 &&
					    f_read(&file, presetName, 13, &bytesRead) == FR_OK && bytesRead == 13)
						bNameLoaded = true;
				}

				if (bNameLoaded)
				{
					strncpy(m_PresetNames[m_nPresetCount], presetName, 12);
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
		
		// Show 2 items at a time (presets + Back/Exit options)
		u8 nTotalEntries = m_nPresetCount + 2;  // Presets + Back + Exit
		for (u8 i = 0; i < 2; ++i)
		{
			u8 entryIndex = m_nPresetListScroll + i;
			if (entryIndex >= nTotalEntries)
				break;

			bool isSelected = (entryIndex == m_nPresetListSelection);
			int16_t y = (i == 0) ? y1 : y2;

			if (isSelected)
				OLED.DrawFilledRect(0, y - 1, 127, y + 8, false);

			// Display entry text
			if (entryIndex < m_nPresetCount)
			{
				// Regular preset entry
				OLED.PrintSmall(m_PresetNames[entryIndex], 2, y, isSelected);
			}
			else if (entryIndex == m_nPresetCount)
			{
				// Back option
				OLED.PrintSmall("< Back", 2, y, isSelected);
			}
			else if (entryIndex == m_nPresetCount + 1)
			{
				// Exit option
				OLED.PrintSmall("X Exit", 2, y, isSelected);
			}
		}
		
		// Draw scroll indicator if there are more entries
		if (nTotalEntries > 2)
		{
			// Show which entries are visible (e.g., "1-2/7" for 5 presets + Back + Exit)
			char scrollInfo[16];
			snprintf(scrollInfo, sizeof(scrollInfo), "%d-%d/%d",
				m_nPresetListScroll + 1,
				(m_nPresetListScroll + 2 <= nTotalEntries) ? m_nPresetListScroll + 2 : nTotalEntries,
				nTotalEntries);
			OLED.PrintSmall(scrollInfo, 108, y2, false);
		}
	}
	else
	{
		// Character LCD
		char line1[21];
		char line2[21];
		u8 nTotalEntries = m_nPresetCount + 2;  // Presets + Back + Exit

		if (m_nPresetListSelection < m_nPresetCount)
		{
			// Displaying a preset
			snprintf(line1, sizeof(line1), "> %.12s", m_PresetNames[m_nPresetListSelection]);
			snprintf(line2, sizeof(line2), "%d/%d", m_nPresetListSelection + 1, nTotalEntries);
		}
		else if (m_nPresetListSelection == m_nPresetCount)
		{
			// Back option
			snprintf(line1, sizeof(line1), "> Back");
			snprintf(line2, sizeof(line2), "%d/%d", m_nPresetListSelection + 1, nTotalEntries);
		}
		else if (m_nPresetListSelection == m_nPresetCount + 1)
		{
			// Exit option
			snprintf(line1, sizeof(line1), "> Exit");
			snprintf(line2, sizeof(line2), "%d/%d", m_nPresetListSelection + 1, nTotalEntries);
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

// FX Settings - Read access
u8 CMenu::GetChannelReverbSend(u8 nChannel) const
{
	return (nChannel < MIDIChannelCount) ? m_ChannelReverbSend[nChannel] : 40;
}

u8 CMenu::GetChannelChorusSend(u8 nChannel) const
{
	return (nChannel < MIDIChannelCount) ? m_ChannelChorusSend[nChannel] : 0;
}

u8 CMenu::GetChannelPan(u8 nChannel) const
{
	return (nChannel < MIDIChannelCount) ? m_ChannelPan[nChannel] : 64;
}

u8 CMenu::GetChannelExpression(u8 nChannel) const
{
	return (nChannel < MIDIChannelCount) ? m_ChannelExpression[nChannel] : 127;
}

void CMenu::NavigateFXSettings(s8 nDelta)
{
	s8 nOption = static_cast<s8>(m_FXSettingsOption) + nDelta;
	if (nOption < 0)
		nOption = static_cast<s8>(TFXSettingsOption::Count) - 1;
	else if (nOption >= static_cast<s8>(TFXSettingsOption::Count))
		nOption = 0;
	m_FXSettingsOption = static_cast<TFXSettingsOption>(nOption);
}

void CMenu::AdjustFXSettingsValue(s8 nDelta)
{
	if (m_FXSettingsOption == TFXSettingsOption::Channel)
	{
		// Change selected channel
		s8 nChannel = static_cast<s8>(m_nSelectedChannel) + nDelta;
		if (nChannel < 0)
			nChannel = MIDIChannelCount - 1;
		else if (nChannel >= MIDIChannelCount)
			nChannel = 0;
		m_nSelectedChannel = static_cast<u8>(nChannel);
	}
	else if (m_FXSettingsOption == TFXSettingsOption::Reverb)
	{
		// Adjust reverb send (0-127)
		s16 nValue = static_cast<s16>(m_ChannelReverbSend[m_nSelectedChannel]) + nDelta;
		if (nValue < 0)
			nValue = 0;
		else if (nValue > 127)
			nValue = 127;
		m_ChannelReverbSend[m_nSelectedChannel] = static_cast<u8>(nValue);

		// Send MIDI CC 91
		m_bPendingFXChange = true;
		m_nFXChangeChannel = m_nSelectedChannel;
		m_nFXChangeCC = 91;
		m_nFXChangeValue = static_cast<u8>(nValue);
	}
	else if (m_FXSettingsOption == TFXSettingsOption::Chorus)
	{
		// Adjust chorus send (0-127)
		s16 nValue = static_cast<s16>(m_ChannelChorusSend[m_nSelectedChannel]) + nDelta;
		if (nValue < 0)
			nValue = 0;
		else if (nValue > 127)
			nValue = 127;
		m_ChannelChorusSend[m_nSelectedChannel] = static_cast<u8>(nValue);

		// Send MIDI CC 93
		m_bPendingFXChange = true;
		m_nFXChangeChannel = m_nSelectedChannel;
		m_nFXChangeCC = 93;
		m_nFXChangeValue = static_cast<u8>(nValue);
	}
	else if (m_FXSettingsOption == TFXSettingsOption::Pan)
	{
		// Adjust pan (0=left, 64=center, 127=right)
		// Reverse delta so turning right decreases value (pans right feels natural)
		s16 nValue = static_cast<s16>(m_ChannelPan[m_nSelectedChannel]) - nDelta;
		if (nValue < 0)
			nValue = 0;
		else if (nValue > 127)
			nValue = 127;
		m_ChannelPan[m_nSelectedChannel] = static_cast<u8>(nValue);

		// Send MIDI CC 10
		m_bPendingFXChange = true;
		m_nFXChangeChannel = m_nSelectedChannel;
		m_nFXChangeCC = 10;
		m_nFXChangeValue = static_cast<u8>(nValue);
	}
	else if (m_FXSettingsOption == TFXSettingsOption::Expression)
	{
		// Adjust expression (0-127)
		s16 nValue = static_cast<s16>(m_ChannelExpression[m_nSelectedChannel]) + nDelta;
		if (nValue < 0)
			nValue = 0;
		else if (nValue > 127)
			nValue = 127;
		m_ChannelExpression[m_nSelectedChannel] = static_cast<u8>(nValue);

		// Send MIDI CC 11
		m_bPendingFXChange = true;
		m_nFXChangeChannel = m_nSelectedChannel;
		m_nFXChangeCC = 11;
		m_nFXChangeValue = static_cast<u8>(nValue);
	}
}

void CMenu::NavigateReverbSettings(s8 nDelta)
{
	s8 nOption = static_cast<s8>(m_ReverbSettingsOption) + nDelta;
	if (nOption < 0)
		nOption = static_cast<s8>(TReverbSettingsOption::Count) - 1;
	else if (nOption >= static_cast<s8>(TReverbSettingsOption::Count))
		nOption = 0;
	m_ReverbSettingsOption = static_cast<TReverbSettingsOption>(nOption);
}

void CMenu::NavigateChorusSettings(s8 nDelta)
{
	s8 nOption = static_cast<s8>(m_ChorusSettingsOption) + nDelta;
	if (nOption < 0)
		nOption = static_cast<s8>(TChorusSettingsOption::Count) - 1;
	else if (nOption >= static_cast<s8>(TChorusSettingsOption::Count))
		nOption = 0;
	m_ChorusSettingsOption = static_cast<TChorusSettingsOption>(nOption);
}

void CMenu::AdjustReverbSettingsValue(s8 nDelta)
{
	if (m_ReverbSettingsOption == TReverbSettingsOption::RoomSize)
	{
		// Adjust room size (0-100, maps to 0.0-1.0)
		s16 nValue = static_cast<s16>(m_nReverbRoomSize) + nDelta;
		if (nValue < 0)
			nValue = 0;
		else if (nValue > 100)
			nValue = 100;
		m_nReverbRoomSize = static_cast<u8>(nValue);

		// Flag global FX change
		m_bPendingGlobalFXChange = true;
		m_PendingGlobalFXParameter = TGlobalFXParameter::ReverbRoomSize;
		m_nPendingGlobalFXValue = static_cast<u8>(nValue);
	}
	else if (m_ReverbSettingsOption == TReverbSettingsOption::Damping)
	{
		// Adjust damping (0-100, maps to 0.0-1.0)
		s16 nValue = static_cast<s16>(m_nReverbDamping) + nDelta;
		if (nValue < 0)
			nValue = 0;
		else if (nValue > 100)
			nValue = 100;
		m_nReverbDamping = static_cast<u8>(nValue);

		// Flag global FX change
		m_bPendingGlobalFXChange = true;
		m_PendingGlobalFXParameter = TGlobalFXParameter::ReverbDamping;
		m_nPendingGlobalFXValue = static_cast<u8>(nValue);
	}
	else if (m_ReverbSettingsOption == TReverbSettingsOption::Width)
	{
		// Adjust width (0-100, maps to 0.0-100.0)
		s16 nValue = static_cast<s16>(m_nReverbWidth) + nDelta;
		if (nValue < 0)
			nValue = 0;
		else if (nValue > 100)
			nValue = 100;
		m_nReverbWidth = static_cast<u8>(nValue);

		// Flag global FX change
		m_bPendingGlobalFXChange = true;
		m_PendingGlobalFXParameter = TGlobalFXParameter::ReverbWidth;
		m_nPendingGlobalFXValue = static_cast<u8>(nValue);
	}
	else if (m_ReverbSettingsOption == TReverbSettingsOption::Level)
	{
		// Adjust level (0-100, maps to 0.0-1.0)
		s16 nValue = static_cast<s16>(m_nReverbLevel) + nDelta;
		if (nValue < 0)
			nValue = 0;
		else if (nValue > 100)
			nValue = 100;
		m_nReverbLevel = static_cast<u8>(nValue);

		// Flag global FX change
		m_bPendingGlobalFXChange = true;
		m_PendingGlobalFXParameter = TGlobalFXParameter::ReverbLevel;
		m_nPendingGlobalFXValue = static_cast<u8>(nValue);
	}
}

void CMenu::AdjustChorusSettingsValue(s8 nDelta)
{
	if (m_ChorusSettingsOption == TChorusSettingsOption::Depth)
	{
		// Adjust depth (0-210, maps to 0.0-21.0, divide by 10)
		s16 nValue = static_cast<s16>(m_nChorusDepth) + nDelta;
		if (nValue < 0)
			nValue = 0;
		else if (nValue > 210)
			nValue = 210;
		m_nChorusDepth = static_cast<u8>(nValue);

		// Flag global FX change
		m_bPendingGlobalFXChange = true;
		m_PendingGlobalFXParameter = TGlobalFXParameter::ChorusDepth;
		m_nPendingGlobalFXValue = static_cast<u8>(nValue);
	}
	else if (m_ChorusSettingsOption == TChorusSettingsOption::Speed)
	{
		// Adjust speed (1-50, maps to 0.1-5.0 Hz, divide by 10)
		s16 nValue = static_cast<s16>(m_nChorusSpeed) + nDelta;
		if (nValue < 1)
			nValue = 1;
		else if (nValue > 50)
			nValue = 50;
		m_nChorusSpeed = static_cast<u8>(nValue);

		// Flag global FX change
		m_bPendingGlobalFXChange = true;
		m_PendingGlobalFXParameter = TGlobalFXParameter::ChorusSpeed;
		m_nPendingGlobalFXValue = static_cast<u8>(nValue);
	}
	else if (m_ChorusSettingsOption == TChorusSettingsOption::Level)
	{
		// Adjust level (0-100, maps to 0.0-10.0, divide by 10)
		s16 nValue = static_cast<s16>(m_nChorusLevel) + nDelta;
		if (nValue < 0)
			nValue = 0;
		else if (nValue > 100)
			nValue = 100;
		m_nChorusLevel = static_cast<u8>(nValue);

		// Flag global FX change
		m_bPendingGlobalFXChange = true;
		m_PendingGlobalFXParameter = TGlobalFXParameter::ChorusLevel;
		m_nPendingGlobalFXValue = static_cast<u8>(nValue);
	}
	else if (m_ChorusSettingsOption == TChorusSettingsOption::Voices)
	{
		// Adjust voices (0-99, direct mapping)
		s16 nValue = static_cast<s16>(m_nChorusVoices) + nDelta;
		if (nValue < 0)
			nValue = 0;
		else if (nValue > 99)
			nValue = 99;
		m_nChorusVoices = static_cast<u8>(nValue);

		// Flag global FX change
		m_bPendingGlobalFXChange = true;
		m_PendingGlobalFXParameter = TGlobalFXParameter::ChorusVoices;
		m_nPendingGlobalFXValue = static_cast<u8>(nValue);
	}
}

void CMenu::DrawReverbSettings(CLCD& LCD) const
{
	const bool bGraphical = (LCD.GetType() == CLCD::TType::Graphical);

	if (bGraphical)
	{
		// OLED Display - 3 row layout
		// Row 0 (y=0):  "REVERB" title
		// Row 1 (y=11): "Rm:" + room size + "Dp:" + damping
		// Row 2 (y=21): "Wd:" + width + "Lv:" + level + buttons

		CSSD1306& OLED = static_cast<CSSD1306&>(LCD);

		const u8 y0 = 0;
		const u8 y1 = 11;
		const u8 y2 = 21;

		// Row 0: Title
		OLED.PrintSmall("REVERB", 0, y0, false);

		// Row 1: Room Size and Damping
		bool roomSizeSelected = (m_ReverbSettingsOption == TReverbSettingsOption::RoomSize);
		bool dampingSelected = (m_ReverbSettingsOption == TReverbSettingsOption::Damping);

		// Room Size label and value
		const u8 roomSizeLabelX = 0;
		const u8 roomSizeValueX = 18;
		OLED.PrintSmall("Rm:", roomSizeLabelX, y1, false);
		char rmStr[4];
		snprintf(rmStr, sizeof(rmStr), "%3d", m_nReverbRoomSize);

		if (roomSizeSelected)
		{
			if (m_bEditing)
			{
				OLED.DrawFilledRect(roomSizeValueX, y1, roomSizeValueX + 17, y1 + 7, false);
			}
			else
			{
				OLED.DrawFilledRect(roomSizeValueX, y1 - 1, roomSizeValueX + 17, y1 - 1, false);
				OLED.DrawFilledRect(roomSizeValueX, y1 + 8, roomSizeValueX + 17, y1 + 8, false);
				OLED.DrawFilledRect(roomSizeValueX, y1 - 1, roomSizeValueX, y1 + 8, false);
				OLED.DrawFilledRect(roomSizeValueX + 17, y1 - 1, roomSizeValueX + 17, y1 + 8, false);
			}
		}
		OLED.PrintSmall(rmStr, roomSizeValueX, y1, roomSizeSelected && m_bEditing);

		// Damping label and value
		const u8 dampingLabelX = 60;
		const u8 dampingValueX = 78;
		OLED.PrintSmall("Dp:", dampingLabelX, y1, false);
		char dpStr[4];
		snprintf(dpStr, sizeof(dpStr), "%3d", m_nReverbDamping);

		if (dampingSelected)
		{
			if (m_bEditing)
			{
				OLED.DrawFilledRect(dampingValueX, y1, dampingValueX + 17, y1 + 7, false);
			}
			else
			{
				OLED.DrawFilledRect(dampingValueX, y1 - 1, dampingValueX + 17, y1 - 1, false);
				OLED.DrawFilledRect(dampingValueX, y1 + 8, dampingValueX + 17, y1 + 8, false);
				OLED.DrawFilledRect(dampingValueX, y1 - 1, dampingValueX, y1 + 8, false);
				OLED.DrawFilledRect(dampingValueX + 17, y1 - 1, dampingValueX + 17, y1 + 8, false);
			}
		}
		OLED.PrintSmall(dpStr, dampingValueX, y1, dampingSelected && m_bEditing);

		// Row 2: Width, Level, buttons
		bool widthSelected = (m_ReverbSettingsOption == TReverbSettingsOption::Width);
		bool levelSelected = (m_ReverbSettingsOption == TReverbSettingsOption::Level);
		bool backSelected = (m_ReverbSettingsOption == TReverbSettingsOption::Back);
		bool exitSelected = (m_ReverbSettingsOption == TReverbSettingsOption::Exit);

		// Width label and value
		const u8 widthLabelX = 0;
		const u8 widthValueX = 18;
		OLED.PrintSmall("Wd:", widthLabelX, y2, false);
		char wdStr[4];
		snprintf(wdStr, sizeof(wdStr), "%3d", m_nReverbWidth);

		if (widthSelected)
		{
			if (m_bEditing)
			{
				OLED.DrawFilledRect(widthValueX, y2, widthValueX + 17, y2 + 7, false);
			}
			else
			{
				OLED.DrawFilledRect(widthValueX, y2 - 1, widthValueX + 17, y2 - 1, false);
				OLED.DrawFilledRect(widthValueX, y2 + 8, widthValueX + 17, y2 + 8, false);
				OLED.DrawFilledRect(widthValueX, y2 - 1, widthValueX, y2 + 8, false);
				OLED.DrawFilledRect(widthValueX + 17, y2 - 1, widthValueX + 17, y2 + 8, false);
			}
		}
		OLED.PrintSmall(wdStr, widthValueX, y2, widthSelected && m_bEditing);

		// Level label and value
		const u8 levelLabelX = 46;
		const u8 levelValueX = 64;
		OLED.PrintSmall("Lv:", levelLabelX, y2, false);
		char lvStr[4];
		snprintf(lvStr, sizeof(lvStr), "%3d", m_nReverbLevel);

		if (levelSelected)
		{
			if (m_bEditing)
			{
				OLED.DrawFilledRect(levelValueX, y2, levelValueX + 17, y2 + 7, false);
			}
			else
			{
				OLED.DrawFilledRect(levelValueX, y2 - 1, levelValueX + 17, y2 - 1, false);
				OLED.DrawFilledRect(levelValueX, y2 + 8, levelValueX + 17, y2 + 8, false);
				OLED.DrawFilledRect(levelValueX, y2 - 1, levelValueX, y2 + 8, false);
				OLED.DrawFilledRect(levelValueX + 17, y2 - 1, levelValueX + 17, y2 + 8, false);
			}
		}
		OLED.PrintSmall(lvStr, levelValueX, y2, levelSelected && m_bEditing);

		// Buttons
		const u8 backX = 98;
		const u8 exitX = 112;

		// BACK button (<)
		if (backSelected)
			OLED.DrawFilledRect(backX, y2 - 1, backX + 12 - 1, y2 + 8, false);
		OLED.PrintSmall("<", backX + 2, y2, backSelected);

		// EXIT button (X)
		if (exitSelected)
			OLED.DrawFilledRect(exitX, y2 - 1, exitX + 12 - 1, y2 + 8, false);
		OLED.PrintSmall("X", exitX + 2, y2, exitSelected);
	}
}

void CMenu::DrawChorusSettings(CLCD& LCD) const
{
	const bool bGraphical = (LCD.GetType() == CLCD::TType::Graphical);

	if (bGraphical)
	{
		// OLED Display - 3 row layout
		// Row 0 (y=0):  "CHORUS" title
		// Row 1 (y=11): "Dp:" + depth + "Sp:" + speed
		// Row 2 (y=21): "Lv:" + level + "Vc:" + voices + buttons

		CSSD1306& OLED = static_cast<CSSD1306&>(LCD);

		const u8 y0 = 0;
		const u8 y1 = 11;
		const u8 y2 = 21;

		// Row 0: Title
		OLED.PrintSmall("CHORUS", 0, y0, false);

		// Row 1: Depth and Speed
		bool depthSelected = (m_ChorusSettingsOption == TChorusSettingsOption::Depth);
		bool speedSelected = (m_ChorusSettingsOption == TChorusSettingsOption::Speed);

		// Depth label and value
		const u8 depthLabelX = 0;
		const u8 depthValueX = 18;
		OLED.PrintSmall("Dp:", depthLabelX, y1, false);
		char dpStr[4];
		snprintf(dpStr, sizeof(dpStr), "%3d", m_nChorusDepth);

		if (depthSelected)
		{
			if (m_bEditing)
			{
				OLED.DrawFilledRect(depthValueX, y1, depthValueX + 17, y1 + 7, false);
			}
			else
			{
				OLED.DrawFilledRect(depthValueX, y1 - 1, depthValueX + 17, y1 - 1, false);
				OLED.DrawFilledRect(depthValueX, y1 + 8, depthValueX + 17, y1 + 8, false);
				OLED.DrawFilledRect(depthValueX, y1 - 1, depthValueX, y1 + 8, false);
				OLED.DrawFilledRect(depthValueX + 17, y1 - 1, depthValueX + 17, y1 + 8, false);
			}
		}
		OLED.PrintSmall(dpStr, depthValueX, y1, depthSelected && m_bEditing);

		// Speed label and value
		const u8 speedLabelX = 60;
		const u8 speedValueX = 78;
		OLED.PrintSmall("Sp:", speedLabelX, y1, false);
		char spStr[4];
		snprintf(spStr, sizeof(spStr), "%3d", m_nChorusSpeed);

		if (speedSelected)
		{
			if (m_bEditing)
			{
				OLED.DrawFilledRect(speedValueX, y1, speedValueX + 17, y1 + 7, false);
			}
			else
			{
				OLED.DrawFilledRect(speedValueX, y1 - 1, speedValueX + 17, y1 - 1, false);
				OLED.DrawFilledRect(speedValueX, y1 + 8, speedValueX + 17, y1 + 8, false);
				OLED.DrawFilledRect(speedValueX, y1 - 1, speedValueX, y1 + 8, false);
				OLED.DrawFilledRect(speedValueX + 17, y1 - 1, speedValueX + 17, y1 + 8, false);
			}
		}
		OLED.PrintSmall(spStr, speedValueX, y1, speedSelected && m_bEditing);

		// Row 2: Level, Voices, buttons
		bool levelSelected = (m_ChorusSettingsOption == TChorusSettingsOption::Level);
		bool voicesSelected = (m_ChorusSettingsOption == TChorusSettingsOption::Voices);
		bool backSelected = (m_ChorusSettingsOption == TChorusSettingsOption::Back);
		bool exitSelected = (m_ChorusSettingsOption == TChorusSettingsOption::Exit);

		// Level label and value
		const u8 levelLabelX = 0;
		const u8 levelValueX = 18;
		OLED.PrintSmall("Lv:", levelLabelX, y2, false);
		char lvStr[4];
		snprintf(lvStr, sizeof(lvStr), "%3d", m_nChorusLevel);

		if (levelSelected)
		{
			if (m_bEditing)
			{
				OLED.DrawFilledRect(levelValueX, y2, levelValueX + 17, y2 + 7, false);
			}
			else
			{
				OLED.DrawFilledRect(levelValueX, y2 - 1, levelValueX + 17, y2 - 1, false);
				OLED.DrawFilledRect(levelValueX, y2 + 8, levelValueX + 17, y2 + 8, false);
				OLED.DrawFilledRect(levelValueX, y2 - 1, levelValueX, y2 + 8, false);
				OLED.DrawFilledRect(levelValueX + 17, y2 - 1, levelValueX + 17, y2 + 8, false);
			}
		}
		OLED.PrintSmall(lvStr, levelValueX, y2, levelSelected && m_bEditing);

		// Voices label and value
		const u8 voicesLabelX = 46;
		const u8 voicesValueX = 64;
		OLED.PrintSmall("Vc:", voicesLabelX, y2, false);
		char vcStr[4];
		snprintf(vcStr, sizeof(vcStr), "%3d", m_nChorusVoices);

		if (voicesSelected)
		{
			if (m_bEditing)
			{
				OLED.DrawFilledRect(voicesValueX, y2, voicesValueX + 17, y2 + 7, false);
			}
			else
			{
				OLED.DrawFilledRect(voicesValueX, y2 - 1, voicesValueX + 17, y2 - 1, false);
				OLED.DrawFilledRect(voicesValueX, y2 + 8, voicesValueX + 17, y2 + 8, false);
				OLED.DrawFilledRect(voicesValueX, y2 - 1, voicesValueX, y2 + 8, false);
				OLED.DrawFilledRect(voicesValueX + 17, y2 - 1, voicesValueX + 17, y2 + 8, false);
			}
		}
		OLED.PrintSmall(vcStr, voicesValueX, y2, voicesSelected && m_bEditing);

		// Buttons
		const u8 backX = 98;
		const u8 exitX = 112;

		// BACK button (<)
		if (backSelected)
			OLED.DrawFilledRect(backX, y2 - 1, backX + 12 - 1, y2 + 8, false);
		OLED.PrintSmall("<", backX + 2, y2, backSelected);

		// EXIT button (X)
		if (exitSelected)
			OLED.DrawFilledRect(exitX, y2 - 1, exitX + 12 - 1, y2 + 8, false);
		OLED.PrintSmall("X", exitX + 2, y2, exitSelected);
	}
}

void CMenu::DrawFXSettings(CLCD& LCD) const
{
	const bool bGraphical = (LCD.GetType() == CLCD::TType::Graphical);

	if (bGraphical)
	{
		// OLED Display - 3 row layout
		// Row 0 (y=0):  "FX SETTINGS" title
		// Row 1 (y=11): "Ch:" + channel + "Rv:" + reverb + "Ch:" + chorus
		// Row 2 (y=21): "Pn:" + pan + visual pan indicator + "Ex:" + expression + buttons

		CSSD1306& OLED = static_cast<CSSD1306&>(LCD);

		const u8 y0 = 0;
		const u8 y1 = 11;
		const u8 y2 = 21;

		// Row 0: Title and R/C buttons
		OLED.PrintSmall("FX SETTINGS", 0, y0, false);

		bool reverbSettingsSelected = (m_FXSettingsOption == TFXSettingsOption::ReverbSettings);
		bool chorusSettingsSelected = (m_FXSettingsOption == TFXSettingsOption::ChorusSettings);

		const u8 rButtonX = 74;
		const u8 cButtonX = 88;

		// R button
		if (reverbSettingsSelected)
			OLED.DrawFilledRect(rButtonX, y0 - 1, rButtonX + 12 - 1, y0 + 8, false);
		OLED.PrintSmall("R", rButtonX + 2, y0, reverbSettingsSelected);

		// C button
		if (chorusSettingsSelected)
			OLED.DrawFilledRect(cButtonX, y0 - 1, cButtonX + 12 - 1, y0 + 8, false);
		OLED.PrintSmall("C", cButtonX + 2, y0, chorusSettingsSelected);

		// Get current channel values
		u8 nReverb = m_ChannelReverbSend[m_nSelectedChannel];
		u8 nChorus = m_ChannelChorusSend[m_nSelectedChannel];
		u8 nPan = m_ChannelPan[m_nSelectedChannel];
		u8 nExpression = m_ChannelExpression[m_nSelectedChannel];

		// Row 1: Channel, Reverb, Chorus
		bool channelSelected = (m_FXSettingsOption == TFXSettingsOption::Channel);
		bool reverbSelected = (m_FXSettingsOption == TFXSettingsOption::Reverb);
		bool chorusSelected = (m_FXSettingsOption == TFXSettingsOption::Chorus);

		// Channel label and value
		const u8 channelLabelX = 0;
		const u8 channelValueX = 18;
		OLED.PrintSmall("Ch:", channelLabelX, y1, false);
		char chStr[4];
		snprintf(chStr, sizeof(chStr), "%2d", m_nSelectedChannel + 1);

		if (channelSelected)
		{
			if (m_bEditing)
			{
				OLED.DrawFilledRect(channelValueX, y1, channelValueX + 11, y1 + 7, false);
			}
			else
			{
				OLED.DrawFilledRect(channelValueX, y1 - 1, channelValueX + 11, y1 - 1, false);
				OLED.DrawFilledRect(channelValueX, y1 + 8, channelValueX + 11, y1 + 8, false);
				OLED.DrawFilledRect(channelValueX, y1 - 1, channelValueX, y1 + 8, false);
				OLED.DrawFilledRect(channelValueX + 11, y1 - 1, channelValueX + 11, y1 + 8, false);
			}
		}
		OLED.PrintSmall(chStr, channelValueX, y1, channelSelected && m_bEditing);

		// Reverb label and value
		const u8 reverbLabelX = 36;
		const u8 reverbValueX = 54;
		OLED.PrintSmall("Rv:", reverbLabelX, y1, false);
		char rvStr[4];
		snprintf(rvStr, sizeof(rvStr), "%3d", nReverb);

		if (reverbSelected)
		{
			if (m_bEditing)
			{
				OLED.DrawFilledRect(reverbValueX, y1, reverbValueX + 17, y1 + 7, false);
			}
			else
			{
				OLED.DrawFilledRect(reverbValueX, y1 - 1, reverbValueX + 17, y1 - 1, false);
				OLED.DrawFilledRect(reverbValueX, y1 + 8, reverbValueX + 17, y1 + 8, false);
				OLED.DrawFilledRect(reverbValueX, y1 - 1, reverbValueX, y1 + 8, false);
				OLED.DrawFilledRect(reverbValueX + 17, y1 - 1, reverbValueX + 17, y1 + 8, false);
			}
		}
		OLED.PrintSmall(rvStr, reverbValueX, y1, reverbSelected && m_bEditing);

		// Chorus label and value
		const u8 chorusLabelX = 77;
		const u8 chorusValueX = 101;
		OLED.PrintSmall("Ch:", chorusLabelX, y1, false);
		char chsStr[4];
		snprintf(chsStr, sizeof(chsStr), "%3d", nChorus);

		if (chorusSelected)
		{
			if (m_bEditing)
			{
				OLED.DrawFilledRect(chorusValueX, y1, chorusValueX + 17, y1 + 7, false);
			}
			else
			{
				OLED.DrawFilledRect(chorusValueX, y1 - 1, chorusValueX + 17, y1 - 1, false);
				OLED.DrawFilledRect(chorusValueX, y1 + 8, chorusValueX + 17, y1 + 8, false);
				OLED.DrawFilledRect(chorusValueX, y1 - 1, chorusValueX, y1 + 8, false);
				OLED.DrawFilledRect(chorusValueX + 17, y1 - 1, chorusValueX + 17, y1 + 8, false);
			}
		}
		OLED.PrintSmall(chsStr, chorusValueX, y1, chorusSelected && m_bEditing);

		// Row 2: Pan with visual indicator, Expression, buttons
		bool panSelected = (m_FXSettingsOption == TFXSettingsOption::Pan);
		bool expressionSelected = (m_FXSettingsOption == TFXSettingsOption::Expression);
		bool nextSelected = (m_FXSettingsOption == TFXSettingsOption::Next);
		bool backSelected = (m_FXSettingsOption == TFXSettingsOption::Back);
		bool exitSelected = (m_FXSettingsOption == TFXSettingsOption::Exit);

		// Pan label and value
		const u8 panLabelX = 0;
		const u8 panValueX = 18;
		OLED.PrintSmall("Pn:", panLabelX, y2, false);
		char panStr[4];
		snprintf(panStr, sizeof(panStr), "%3d", nPan);

		if (panSelected)
		{
			if (m_bEditing)
			{
				OLED.DrawFilledRect(panValueX, y2, panValueX + 17, y2 + 7, false);
			}
			else
			{
				OLED.DrawFilledRect(panValueX, y2 - 1, panValueX + 17, y2 - 1, false);
				OLED.DrawFilledRect(panValueX, y2 + 8, panValueX + 17, y2 + 8, false);
				OLED.DrawFilledRect(panValueX, y2 - 1, panValueX, y2 + 8, false);
				OLED.DrawFilledRect(panValueX + 17, y2 - 1, panValueX + 17, y2 + 8, false);
			}
		}
		OLED.PrintSmall(panStr, panValueX, y2, panSelected && m_bEditing);

		// Expression label and value
		const u8 exprLabelX = 42;
		const u8 exprValueX = 60;
		OLED.PrintSmall("Ex:", exprLabelX, y2, false);
		char exStr[4];
		snprintf(exStr, sizeof(exStr), "%3d", nExpression);

		if (expressionSelected)
		{
			if (m_bEditing)
			{
				OLED.DrawFilledRect(exprValueX, y2, exprValueX + 17, y2 + 7, false);
			}
			else
			{
				OLED.DrawFilledRect(exprValueX, y2 - 1, exprValueX + 17, y2 - 1, false);
				OLED.DrawFilledRect(exprValueX, y2 + 8, exprValueX + 17, y2 + 8, false);
				OLED.DrawFilledRect(exprValueX, y2 - 1, exprValueX, y2 + 8, false);
				OLED.DrawFilledRect(exprValueX + 17, y2 - 1, exprValueX + 17, y2 + 8, false);
			}
		}
		OLED.PrintSmall(exStr, exprValueX, y2, expressionSelected && m_bEditing);

		// Buttons - draw with selection highlighting matching other menus
		const u8 backX = 84;
		const u8 nextX = 98;
		const u8 exitX = 112;

		// BACK button (<)
		if (backSelected)
			OLED.DrawFilledRect(backX, y2 - 1, backX + 12 - 1, y2 + 8, false);
		OLED.PrintSmall("<", backX + 2, y2, backSelected);

		// NEXT button (>)
		if (nextSelected)
			OLED.DrawFilledRect(nextX, y2 - 1, nextX + 12 - 1, y2 + 8, false);
		OLED.PrintSmall(">", nextX + 2, y2, nextSelected);

		// EXIT button (X)
		if (exitSelected)
			OLED.DrawFilledRect(exitX, y2 - 1, exitX + 12 - 1, y2 + 8, false);
		OLED.PrintSmall("X", exitX + 2, y2, exitSelected);
	}
	else
	{
		// Character LCD - 2 line display
		char line1[21];
		char line2[21];

		// Get values
		u8 nReverb = m_ChannelReverbSend[m_nSelectedChannel];
		u8 nChorus = m_ChannelChorusSend[m_nSelectedChannel];
		u8 nPan = m_ChannelPan[m_nSelectedChannel];
		u8 nExpression = m_ChannelExpression[m_nSelectedChannel];

		switch (m_FXSettingsOption)
		{
			case TFXSettingsOption::Channel:
				snprintf(line1, sizeof(line1), "FX Ch%d", m_nSelectedChannel + 1);
				snprintf(line2, sizeof(line2), "Select channel");
				break;

			case TFXSettingsOption::Reverb:
				snprintf(line1, sizeof(line1), "Reverb Ch%d", m_nSelectedChannel + 1);
				snprintf(line2, sizeof(line2), "%d", nReverb);
				break;

			case TFXSettingsOption::Chorus:
				snprintf(line1, sizeof(line1), "Chorus Ch%d", m_nSelectedChannel + 1);
				snprintf(line2, sizeof(line2), "%d", nChorus);
				break;

			case TFXSettingsOption::Pan:
				snprintf(line1, sizeof(line1), "Pan Ch%d", m_nSelectedChannel + 1);
				// Show L-C-R indicator
				if (nPan < 56)
					snprintf(line2, sizeof(line2), "L %d", nPan);
				else if (nPan > 72)
					snprintf(line2, sizeof(line2), "R %d", nPan);
				else
					snprintf(line2, sizeof(line2), "C %d", nPan);
				break;

			case TFXSettingsOption::Expression:
				snprintf(line1, sizeof(line1), "Expression Ch%d", m_nSelectedChannel + 1);
				snprintf(line2, sizeof(line2), "%d", nExpression);
				break;

			case TFXSettingsOption::Next:
				snprintf(line1, sizeof(line1), "Next Page");
				snprintf(line2, sizeof(line2), "Anim settings");
				break;

			case TFXSettingsOption::Back:
				snprintf(line1, sizeof(line1), "Back");
				snprintf(line2, sizeof(line2), "Main menu");
				break;

			case TFXSettingsOption::Exit:
				snprintf(line1, sizeof(line1), "Exit Menu");
				line2[0] = '\0';
				break;

			default:
				snprintf(line1, sizeof(line1), "FX Settings");
				line2[0] = '\0';
				break;
		}

		LCD.Print(line1, 0, 0, true);
		LCD.Print(line2, 0, 1, true);
	}
}
