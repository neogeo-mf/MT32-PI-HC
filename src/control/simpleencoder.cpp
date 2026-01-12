//
// simpleencoder.cpp
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
// mt32-pi. 
//

#include "control/button.h"
#include "control/control.h"
#include <circle/timer.h>
#include "utility.h"  // for Utility::TicksToMillis()

constexpr u8 GPIOPinButton1         = 17;
constexpr u8 GPIOPinButton2         = 27;
constexpr u8 GPIOPinEncoderButton   = 4;
constexpr u8 GPIOPinEncoderCLK      = 22;
constexpr u8 GPIOPinEncoderDAT      = 23;

// Minimum button press duration (ms) required for a valid click
// This filters out EMI noise spikes which are typically very brief
// Reduced to 100ms to test responsiveness with repeat event filter in place
constexpr u32 kMinimumPressDurationMs = 100;
// maximum interval (ms) between two presses to count as a double‑click
constexpr u32 kDoubleClickWindowMs   = 300;
// how long to wait before confirming a single‑click
constexpr u32 kSingleClickTimeoutMs  = 300;

constexpr u8 ButtonMask = (1 << static_cast<u8>(TButton::Button1)) |
                          (1 << static_cast<u8>(TButton::Button2)) |
                          (1 << static_cast<u8>(TButton::EncoderButton));

CControlSimpleEncoder::CControlSimpleEncoder(TEventQueue& pEventQueue,
                                             CRotaryEncoder::TEncoderType EncoderType,
                                             bool bEncoderReversed)
    : CControl(pEventQueue),
      m_GPIOEncoderButton(GPIOPinEncoderButton, TGPIOMode::GPIOModeInputPullUp),
      m_GPIOButton1       (GPIOPinButton1,       TGPIOMode::GPIOModeInputPullUp),
      m_GPIOButton2       (GPIOPinButton2,       TGPIOMode::GPIOModeInputPullUp),
      m_Encoder           (EncoderType, bEncoderReversed, GPIOPinEncoderCLK, GPIOPinEncoderDAT),
      m_LastEncoderClickTime(0),
      m_LastEncoderRotationTime(0),
      m_EncoderWasPressed(false)
{
}

void CControlSimpleEncoder::Update()
{
    // Temporarily mask out the encoder button bit before calling parent Update()
    // This prevents the parent from generating immediate press/release events for the encoder button
    // (we handle encoder button clicks with delayed single/double-click detection in ReadGPIOPins)
    const u8 nEncoderButtonMask = (1 << static_cast<u8>(TButton::EncoderButton));
    const u8 nSavedButtonState = m_nButtonState;
    const u8 nSavedLastButtonState = m_nLastButtonState;

    // Mask out encoder button from button states
    m_nButtonState &= ~nEncoderButtonMask;
    m_nLastButtonState &= ~nEncoderButtonMask;

    // Call parent to handle Button1 and Button2
    CControl::Update();

    // Restore original button states
    m_nButtonState = nSavedButtonState;
    m_nLastButtonState = nSavedLastButtonState;

    // Check if encoder button is currently pressed
    const bool bEncoderButtonPressed = (m_nButtonState & nEncoderButtonMask) != 0;

    // Only process encoder rotation if button is NOT pressed
    // This prevents glitchy behavior when rotating while holding the button
    if (!bEncoderButtonPressed)
    {
        const s8 nEncoderDelta = m_Encoder.Read();
        if (nEncoderDelta != 0)
        {
            // Record rotation timestamp for button lockout
            m_LastEncoderRotationTime = CTimer::GetClockTicks();

            TEvent Event;
            Event.Type          = TEventType::Encoder;
            Event.Encoder.nDelta = nEncoderDelta;
            m_pEventQueue->Enqueue(Event);
        }
    }
    else
    {
        // Button is pressed - still read encoder to clear any pending ticks
        // but don't generate events
        m_Encoder.Read();
    }
}

void CControlSimpleEncoder::ReadGPIOPins()
{
    // 1) Read & debounce
    const u32 nGPIOState = CGPIOPin::ReadAll();
    const u8 nButtonState =
        (((nGPIOState >> GPIOPinButton1)       & 1) << static_cast<u8>(TButton::Button1))       |
        (((nGPIOState >> GPIOPinButton2)       & 1) << static_cast<u8>(TButton::Button2))       |
        (((nGPIOState >> GPIOPinEncoderButton) & 1) << static_cast<u8>(TButton::EncoderButton));

    DebounceButtonState(nButtonState, ButtonMask);

    // Use the DEBOUNCED button state, not the raw state
    const bool bPressed = (m_nButtonState & (1 << static_cast<u8>(TButton::EncoderButton))) != 0;
    const u32  now      = CTimer::GetClockTicks();

    // Static variables for edge detection and noise immunity
    static bool buttonDown = false;
    static u32 lastPressTime = 0;         // When we first detected button press
    static u32 lastReleaseTime = 0;       // When we last detected button release
    static bool pressConfirmed = false;   // True when press is stable enough to process
    static u32 rotationTimeWhenPressed = 0;  // m_LastEncoderRotationTime when button was pressed

    // Circuit breaker: track rapid clicks to detect phantom click loops
    static u32 clickHistory[5] = {0, 0, 0, 0, 0};  // Timestamps of last 5 clicks
    static u8 clickHistoryIndex = 0;
    static u32 panicModeUntil = 0;  // If non-zero, ignore all input until this time

    // Minimal stability for clean button detection (10ms is imperceptible)
    constexpr u32 kPressStabilityMs = 10;
    constexpr u32 kReleaseStabilityMs = 10;
    // Rotation lockout: ignore button presses within this window after encoder rotation
    // This prevents EMI noise from encoder rotation from triggering phantom clicks
    constexpr u32 kRotationLockoutMs = 150;
    // Circuit breaker: if 5 clicks happen within this window, enter panic mode
    constexpr u32 kPanicWindowMs = 1000;
    constexpr u32 kPanicDurationMs = 500;

    // PANIC MODE: If we're in panic mode, ignore all input until timeout expires
    if (panicModeUntil != 0)
    {
        if (Utility::TicksToMillis(now - panicModeUntil) < kPanicDurationMs)
        {
            // Still in panic mode - reset all state and ignore input
            buttonDown = false;
            pressConfirmed = false;
            lastPressTime = 0;
            lastReleaseTime = 0;
            m_EncoderWasPressed = false;
            return;
        }
        else
        {
            // Panic mode expired - reset and resume normal operation
            panicModeUntil = 0;
            clickHistoryIndex = 0;
            for (u8 i = 0; i < 5; ++i)
                clickHistory[i] = 0;
        }
    }

    // 2) Edge detect on encoder-button with noise immunity
    if (bPressed)
    {
        if (!buttonDown)
        {
            // Button just pressed - record time and rotation state
            buttonDown = true;
            lastPressTime = now;
            pressConfirmed = false;
            rotationTimeWhenPressed = m_LastEncoderRotationTime;
        }
        else if (!pressConfirmed)
        {
            // Button is still pressed - check if it's been stable long enough
            u32 pressDurationMs = Utility::TicksToMillis(now - lastPressTime);
            if (pressDurationMs >= kPressStabilityMs)
            {
                // Press is stable - confirm it
                // We'll check rotation lockout AND press duration when button is released
                // (this allows legitimate long presses to work even during rotation)
                pressConfirmed = true;
            }
        }
        // Reset release tracking
        lastReleaseTime = 0;
    }
    else  // !bPressed
    {
        if (buttonDown)
        {
            // Button was down, now released - track release time
            if (lastReleaseTime == 0)
            {
                lastReleaseTime = now;
            }
            else
            {
                // Check if release has been stable
                u32 releaseDurationMs = Utility::TicksToMillis(now - lastReleaseTime);
                if (releaseDurationMs >= kReleaseStabilityMs)
                {
                    // Release confirmed - check if press was long enough to count
                    if (pressConfirmed)
                    {
                        // Calculate how long the button was held
                        u32 pressDurationMs = Utility::TicksToMillis(lastReleaseTime - lastPressTime);

                        // Check rotation lockout FIRST, but only for short presses
                        // Long presses (>= 200ms) bypass rotation lockout (they're likely legitimate)
                        if (pressDurationMs < kMinimumPressDurationMs)
                        {
                            // Short press - check rotation lockout
                            u32 timeSinceRotation = Utility::TicksToMillis(now - m_LastEncoderRotationTime);
                            if (m_LastEncoderRotationTime != 0 && timeSinceRotation < kRotationLockoutMs)
                            {
                                // Within lockout window and short press - likely phantom click
                                buttonDown = false;
                                pressConfirmed = false;
                                lastReleaseTime = 0;
                                return;
                            }

                            // Short press but outside rotation lockout window - still reject (too short)
                            buttonDown = false;
                            pressConfirmed = false;
                            lastReleaseTime = 0;
                            return;
                        }

                        // Long press (>= 200ms) - bypasses rotation lockout, likely legitimate
                        // Check if rotation occurred during press (could indicate EMI)
                        if (m_LastEncoderRotationTime != rotationTimeWhenPressed)
                        {
                            // Rotation detected during press - could be user rotating while holding button
                            // But we'll still reject this to be safe
                            buttonDown = false;
                            pressConfirmed = false;
                            lastReleaseTime = 0;
                            return;
                        }

                        // CIRCUIT BREAKER: Check if we're clicking too rapidly BEFORE adding to history
                        // This prevents the click from being registered at all
                        u32 oldestClick = clickHistory[clickHistoryIndex];
                        if (oldestClick != 0)
                        {
                            u32 timeSpan = Utility::TicksToMillis(now - oldestClick);
                            if (timeSpan < kPanicWindowMs)
                            {
                                // PANIC! Would be 5 clicks in under 1 second = phantom click loop
                                // DON'T add this click to history, DON'T fire event, enter panic mode
                                panicModeUntil = now;
                                buttonDown = false;
                                pressConfirmed = false;
                                lastReleaseTime = 0;
                                return;
                            }
                        }

                        // Add to click history AFTER checking for panic
                        clickHistory[clickHistoryIndex] = now;
                        clickHistoryIndex = (clickHistoryIndex + 1) % 5;

                        // Fire single-click event immediately on release
                        TEvent Event;
                        Event.Type                = TEventType::Button;
                        Event.Button.Button       = TButton::EncoderButton;
                        Event.Button.bPressed     = true;
                        Event.Button.bRepeat      = false;
                        Event.Button.bDoubleClick = false;
                        m_pEventQueue->Enqueue(Event);
                        m_LastEncoderClickTime = now;
                    }

                    // Reset state
                    buttonDown = false;
                    pressConfirmed = false;
                    lastReleaseTime = 0;
                }
            }
        }
    }

    // 3) Read rotary ticks
    m_Encoder.ReadGPIOPins(
        (nGPIOState >> GPIOPinEncoderCLK) & 1,
        (nGPIOState >> GPIOPinEncoderDAT) & 1
    );
}
