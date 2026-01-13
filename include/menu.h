//
// menu.h
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

#ifndef _menu_h
#define _menu_h

#include <circle/types.h>

class CLCD;

// MIDI channel settings menu - single screen with all options visible
class CMenu
{
public:
	enum class TMenuOption
	{
		Channel,    // Select which channel (1-16)
		Mute,       // Mute on/off
		Volume,     // Channel volume
		Program,    // Program/patch number
		Route,      // Route to different channel
		Next,       // Go to next settings page
		Exit,       // Exit menu
		Count
	};

	enum class TVisualizationMode
	{
		BarGraph,      // MIDI bar graph visualization
		Animation,     // Cute face animation mode
		Asteroids,     // MIDI-reactive asteroids game
		MatrixRain,    // Matrix-style falling characters
		Oscilloscope,  // Real-time waveform display
		Count
	};

	enum class TAnimSettingsOption
	{
		Mode,       // Select visualization mode
		Next,       // Go to preset menu
		Back,       // Go back to main menu
		Exit,       // Exit menu
		Count
	};

	enum class TPresetMenuOption
	{
		Save,       // Save current settings as preset
		Load,       // Load preset from file
		Delete,     // Delete a preset
		Back,       // Go back to animation settings
		Exit,       // Exit menu
		Count
	};

	enum class TPresetMenuScreen
	{
		Main,       // Main preset menu (Save/Load/Delete/Back/Exit)
		NameEditor, // Name editor for saving preset
		PresetList, // List of presets for Load/Delete
		Confirm     // Confirmation dialog for overwrite/delete
	};

	enum class TNameEditorOption
	{
		Character0, Character1, Character2, Character3,
		Character4, Character5, Character6, Character7,
		Character8, Character9, Character10, Character11,
		Save,       // Save button
		Cancel,     // Cancel button
		Count
	};

	enum class TPresetListOperation
	{
		Load,       // Loading a preset
		Delete      // Deleting a preset
	};

	struct MIDIPreset
	{
		char Name[13];                  // 12 chars + null terminator
		u16 MutedChannels;              // Bit field: bit N = channel N muted
		u8 ChannelRoute[16];
		u8 ChannelVolume[16];
		u8 ChannelProgram[16];          // Program/patch number per channel (0-127)
	};

	CMenu();

	// State management
	void EnterMenu();
	void ExitMenu();
	void LoadVisualizationMode();
	void SaveVisualizationMode();
	bool IsActive() const { return m_bActive; }
	bool IsEditing() const { return m_bEditing; }

	// Navigation
	void Navigate(s8 nDelta);  // Encoder rotation - navigate options or change value
	void Select();              // Encoder button click - enter/exit edit mode

	// Rendering - draw the menu directly on LCD
	void Draw(CLCD& LCD) const;

	// MIDI channel settings - read access
	u8 GetSelectedChannel() const { return m_nSelectedChannel; }
	TMenuOption GetSelectedOption() const { return m_SelectedOption; }
	TVisualizationMode GetVisualizationMode() const { return m_VisualizationMode; }
	bool IsChannelMuted(u8 nChannel) const;
	u8 GetChannelRoute(u8 nChannel) const;
	u8 GetChannelVolume(u8 nChannel) const;
	u8 GetChannelProgram(u8 nChannel) const;

	// Program change notification
	bool HasPendingProgramChange() const { return m_bPendingProgramChange; }
	u8 GetPendingProgramChangeChannel() const { return m_nProgramChangeChannel; }
	void ClearPendingProgramChange() { m_bPendingProgramChange = false; }
	bool NeedsAllProgramsSent() const { return m_bSendAllPrograms; }
	void ClearSendAllPrograms() { m_bSendAllPrograms = false; }

private:
	static constexpr u8 MIDIChannelCount = 16;
	static constexpr u8 DefaultChannelVolume = 127;

	bool m_bActive;
	bool m_bEditing;            // True when actively editing a value
	bool m_bShowAnimSettings;   // True when showing animation settings screen
	bool m_bShowPresetMenu;     // True when showing preset menu
	u8 m_nSelectedChannel;      // 0-15
	TMenuOption m_SelectedOption;
	TAnimSettingsOption m_AnimSettingsOption;
	TVisualizationMode m_VisualizationMode;
	TPresetMenuOption m_PresetMenuOption;
	TPresetMenuScreen m_PresetMenuScreen;
	TPresetListOperation m_PresetListOperation;
	TNameEditorOption m_NameEditorOption;
	unsigned m_nLastSelectTime; // Timestamp of last Select() call for debouncing
	unsigned m_nLastEditToggleTime; // Timestamp of last edit mode toggle (for EMI debouncing)

	// Toggle loop detection - track rapid edit mode changes
	unsigned m_EditToggleHistory[5];  // Timestamps of last 5 edit toggles
	u8 m_nEditToggleIndex;

	// Preset menu state
	char m_PresetNameBuffer[13];    // Name being edited
	u8 m_nPresetListScroll;         // Scroll position in preset list
	u8 m_nPresetListSelection;      // Selected preset in list (0-based)
	u8 m_nPresetCount;              // Total number of presets
	bool m_bConfirmYes;             // True = Yes selected, False = No selected

	// Per-channel MIDI settings
	u16 m_nMutedChannels;           // Bit field: bit N = channel N muted
	u8 m_ChannelRoute[MIDIChannelCount];
	u8 m_ChannelVolume[MIDIChannelCount];
	u8 m_ChannelProgram[MIDIChannelCount];  // Program/patch per channel (0-127)

	// Program change tracking
	bool m_bPendingProgramChange;
	u8 m_nProgramChangeChannel;
	bool m_bSendAllPrograms;

	void NavigateOptions(s8 nDelta);
	void NavigateAnimSettings(s8 nDelta);
	void NavigatePresetMenu(s8 nDelta);
	void AdjustValue(s8 nDelta);
	void AdjustAnimSettingsValue(s8 nDelta);
	void DrawMainMenu(CLCD& LCD) const;
	void DrawAnimSettings(CLCD& LCD) const;
	void DrawPresetMenu(CLCD& LCD) const;
	void DrawPresetMainMenu(CLCD& LCD) const;
	void DrawNameEditor(CLCD& LCD) const;
	void DrawPresetList(CLCD& LCD) const;
	void DrawConfirmation(CLCD& LCD) const;

	// Preset file operations
	bool SavePreset(const char* pName);
	bool LoadPreset(const char* pName);
	bool DeletePreset(const char* pName);
	void LoadPresetList();
	void InitializeDefaultPreset();
	void SanitizeFileName(char* pDest, const char* pSrc, size_t nMaxLen);
	bool PresetExists(const char* pName);

	// Preset name list for display
	static constexpr u8 MaxPresets = 32;
	char m_PresetNames[MaxPresets][13];
};

#endif
