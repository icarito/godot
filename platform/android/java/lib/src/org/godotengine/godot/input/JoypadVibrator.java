/**************************************************************************/
/*  JoypadVibrator.java                                                   */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

package org.godotengine.godot.input;

import android.annotation.SuppressLint;
import android.os.Build;
import android.os.CombinedVibration;
import android.os.VibrationEffect;
import android.os.Vibrator;
import android.os.VibratorManager;
import android.view.InputDevice;

/**
 * Plays Input.start_joy_vibration() on a game controller's own motors (not the phone's).
 *
 * Android 12+ exposes each motor through the device's VibratorManager, so Godot's two
 * magnitudes drive two motors when the controller has them: the first vibrator gets the strong
 * (low-frequency) magnitude and the second the weak one, the usual left/right layout of a
 * gamepad. Older versions only have InputDevice.getVibrator(), a single motor that gets the
 * stronger of the two.
 */
public final class JoypadVibrator {
	// Godot's duration 0 means "until stop_joy_vibration()".
	private static final long UNTIL_STOPPED_MS = 60_000;

	private JoypadVibrator() {}

	@SuppressLint("MissingPermission")
	@SuppressWarnings("deprecation") // InputDevice.getVibrator() is the only way below Android 12
	public static void vibrate(InputDevice device, float weakMagnitude, float strongMagnitude, float durationSec) {
		if (device == null) {
			return;
		}
		boolean stop = weakMagnitude <= 0f && strongMagnitude <= 0f;
		long durationMs = durationSec > 0f ? Math.max(1, Math.round(durationSec * 1000f)) : UNTIL_STOPPED_MS;

		if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
			VibratorManager manager = device.getVibratorManager();
			int[] ids = manager.getVibratorIds();
			if (ids.length == 0) {
				return;
			}
			if (stop) {
				manager.cancel();
				return;
			}
			CombinedVibration.ParallelCombination motors = CombinedVibration.startParallel();
			if (ids.length >= 2) {
				boolean any = false;
				if (strongMagnitude > 0f) {
					motors.addVibrator(ids[0], VibrationEffect.createOneShot(durationMs, amplitude(strongMagnitude)));
					any = true;
				}
				if (weakMagnitude > 0f) {
					motors.addVibrator(ids[1], VibrationEffect.createOneShot(durationMs, amplitude(weakMagnitude)));
					any = true;
				}
				if (!any) {
					return;
				}
			} else {
				float magnitude = Math.max(weakMagnitude, strongMagnitude);
				motors.addVibrator(ids[0], VibrationEffect.createOneShot(durationMs, amplitude(magnitude)));
			}
			manager.vibrate(motors.combine());
			return;
		}

		Vibrator vibrator = device.getVibrator();
		if (vibrator == null || !vibrator.hasVibrator()) {
			return;
		}
		if (stop) {
			vibrator.cancel();
			return;
		}
		if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
			int amp = vibrator.hasAmplitudeControl() ? amplitude(Math.max(weakMagnitude, strongMagnitude)) : VibrationEffect.DEFAULT_AMPLITUDE;
			vibrator.vibrate(VibrationEffect.createOneShot(durationMs, amp));
		} else {
			vibrator.vibrate(durationMs);
		}
	}

	// VibrationEffect amplitudes run 1-255; 0 is not a valid one-shot amplitude.
	private static int amplitude(float magnitude) {
		return Math.max(1, Math.min(255, Math.round(magnitude * 255f)));
	}
}
