/*
 * Copyright © 2016-2024 Synthstrom Audible Limited
 * Copyright © 2024 Madeline Scyphers
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

#include "gui/ui/keyboard/layout/chord_keyboard.h"
#include "gui/colour/colour.h"
#include "gui/ui/audio_recorder.h"
#include "gui/ui/browser/sample_browser.h"
#include "gui/ui/sound_editor.h"
#include "hid/display/display.h"
#include "io/debug/log.h"
#include "model/settings/runtime_feature_settings.h"
#include "util/functions.h"
#include <stdlib.h>

namespace deluge::gui::ui::keyboard::layout {

void KeyboardLayoutChord::evaluatePads(PressedPad presses[kMaxNumKeyboardPadPresses]) {
	currentNotesState = NotesState{}; // Erase active notes
	KeyboardStateChord& state = getState().chord;

	// We run through the presses in reverse order to display the most recent pressed chord on top
	for (int32_t idxPress = kMaxNumKeyboardPadPresses - 1; idxPress >= 0; --idxPress) {

		PressedPad pressed = presses[idxPress];
		if (pressed.active && pressed.x < kDisplayWidth) {

			int32_t chordNo = pressed.y + state.chordList.chordRowOffset;

			Voicing voicing = state.chordList.getChordVoicing(chordNo);
			drawChordName(noteFromCoords(pressed.x), state.chordList.chords[chordNo].name, voicing.supplementalName);

			for (int i = 0; i < kMaxChordKeyboardSize; i++) {
				int32_t offset = voicing.offsets[i];
				if (offset == NONE) {
					continue;
				}
				enableNote(noteFromCoords(pressed.x) + offset, velocity);
			}
		}
	}
	ColumnControlsKeyboard::evaluatePads(presses);
}

void KeyboardLayoutChord::handleVerticalEncoder(int32_t offset) {
	if (verticalEncoderHandledByColumns(offset)) {
		return;
	}
	KeyboardStateChord& state = getState().chord;

	state.chordList.adjustChordRowOffset(offset);
	precalculate();
}

void KeyboardLayoutChord::handleHorizontalEncoder(int32_t offset, bool shiftEnabled,
                                                  PressedPad presses[kMaxNumKeyboardPadPresses], bool encoderPressed) {
	if (horizontalEncoderHandledByColumns(offset, shiftEnabled)) {
		return;
	}
	KeyboardStateChord& state = getState().chord;

	if (encoderPressed) {
		for (int32_t idxPress = kMaxNumKeyboardPadPresses - 1; idxPress >= 0; --idxPress) {

			PressedPad pressed = presses[idxPress];
			if (pressed.active && pressed.x < kDisplayWidth) {

				int32_t chordNo = pressed.y + state.chordList.chordRowOffset;

				state.chordList.adjustVoicingOffset(chordNo, offset);
			}
		}
	}
	else {
		state.noteOffset += offset;
	}
	precalculate();
}

void KeyboardLayoutChord::precalculate() {
	KeyboardStateChord& state = getState().chord;
	if (!initializedNoteOffset) {
		initializedNoteOffset = true;
		state.noteOffset += getRootNote();
	}

	// Pre-Buffer colours for next renderings
	for (int32_t y = 0; y < kDisplayHeight; ++y) {
		int32_t chordNo = y + state.chordList.chordRowOffset;
		ChordQuality quality = state.chordList.chords[chordNo].quality;
		padQualityColours[y] = qualityColours[quality];
	}
	for (int32_t i = 0; i < noteColours.size(); ++i) {
		noteColours[i] = getNoteColour(((state.noteOffset + i) % state.rowInterval) * state.rowColorMultiplier);
	}
	uint8_t hueStepSize = 192 / (kVerticalPages - 1); // 192 is the hue range for the rainbow
	for (int32_t i = 0; i < pageColours.size(); ++i) {
		pageColours[i] = getNoteColour(i * hueStepSize);
	}
}

void KeyboardLayoutChord::renderPads(RGB image[][kDisplayWidth + kSideBarWidth]) {
	KeyboardStateChord& state = getState().chord;

	for (int32_t y = 0; y < kDisplayHeight; ++y) {

		for (int32_t x = 0; x < kDisplayWidth; x++) {
			int32_t chordNo = y + state.chordList.chordRowOffset;
			int32_t pageNo = std::min<int32_t>(chordNo / kDisplayHeight, kVerticalPages - 1);

			if (getScaleModeEnabled()) {
				NoteSet& scaleNotes = getScaleNotes();
				int32_t noteCode = noteFromCoords(x);
				uint16_t noteWithinScale = (uint16_t)(noteCode - getRootNote()) % kOctaveSize;

				// if in scale, color the column
				if (scaleNotes.has(noteWithinScale)) {
					// image[y][x] = noteColours[y];
					image[y][x] = noteColours[x % noteColours.size()];
					// D_PRINTLN("node within scale %d", noteWithinScale);
					// if (noteWithinScale == 0) {
					// 	image[y][x] = noteColours[x % noteColours.size()];
					// }
					// else {
					// 	image[y][x] = noteColours[x % noteColours.size()];
					// 	// image[y][x] = pageColours[pageNo].rotate();
					// }

					// image[y][x] = noteColours[x % noteColours.size()];
				}
				// color the column the page color
				else {
					image[y][x] = pageColours[pageNo].dim(5);
				}
			}
			// if not in scale, color the entire column
			else {
				image[y][x] = noteColours[x % noteColours.size()];
			}

			// We add a colored page color rows to the top and bottom of a page to help with navigation
			// And to the 2nd to last column (2nd to last col will be overwritten in scale mode)
			// We also use different colors for each page
			if ((x == kDisplayWidth - 2)               // show we've reached the start of a new page
			    || (chordNo % 8 == 0)                  // show we've reached the start of a new page
			    || (chordNo % 8 == kDisplayHeight - 1) // show we've reached the end of a page
			    || (chordNo == kUniqueChords - 1)      // show we've reach the very top
			) {
				image[y][x] = pageColours[pageNo].dim(5);
			}
			// We add chord quality to the rightmost column, we also do this last so it overwrites the other colors
			if (x == kDisplayWidth - 1) {
				image[y][x] = padQualityColours[y];
			}
		}
	}
}

void KeyboardLayoutChord::drawChordName(int16_t noteCode, const char* chordName, const char* voicingName) {
	char noteName[3] = {0};
	int32_t isNatural = 1; // gets modified inside noteCodeToString to be 0 if sharp.
	noteCodeToString(noteCode, noteName, &isNatural, false);

	char fullChordName[300];

	if (voicingName && *voicingName) {
		sprintf(fullChordName, "%s%s - %s", noteName, chordName, voicingName);
	}
	else {
		sprintf(fullChordName, "%s%s", noteName, chordName);
	}

	if (display->haveOLED()) {
		display->popupTextTemporary(fullChordName);
	}
	else {
		int8_t drawDot = !isNatural ? 0 : 255;
		display->setScrollingText(fullChordName, 0);
	}
}

bool KeyboardLayoutChord::allowSidebarType(ColumnControlFunction sidebarType) {
	if ((sidebarType == ColumnControlFunction::CHORD) || (sidebarType == ColumnControlFunction::DX)) {
		return false;
	}
	return true;
}

} // namespace deluge::gui::ui::keyboard::layout
