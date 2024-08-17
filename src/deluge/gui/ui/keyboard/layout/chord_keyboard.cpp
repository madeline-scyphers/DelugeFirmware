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
#include "model/scale/note_set.h"
#include "model/settings/runtime_feature_settings.h"
#include "util/functions.h"
#include <stdlib.h>

namespace deluge::gui::ui::keyboard::layout {

void KeyboardLayoutChord::evaluatePads(PressedPad presses[kMaxNumKeyboardPadPresses]) {
	currentNotesState = NotesState{}; // Erase active notes
	KeyboardStateChord& state = getState().chord;
	Voicing voicing;
	const char* name;

	// We run through the presses in reverse order to display the most recent pressed chord on top
	for (int32_t idxPress = kMaxNumKeyboardPadPresses - 1; idxPress >= 0; --idxPress) {

		PressedPad pressed = presses[idxPress];
		if (pressed.active && pressed.x < kDisplayWidth) {
			int16_t noteCode = noteFromCoords(pressed.x, pressed.y);
			if (pressed.y < state.rootRows) {
				voicing = {0, NONE, NONE, NONE, NONE, NONE, NONE};
				name = "";
			}
			else {
				int32_t chordNo = getChordNo(pressed.y);
				name = state.chordList.chords[chordNo].name;
				voicing = state.chordList.getChordVoicing(chordNo);
			}
			drawChordName(noteCode, name, voicing.supplementalName);

			for (int i = 0; i < kMaxChordKeyboardSize; i++) {
				int32_t offset = voicing.offsets[i];
				if (offset == NONE) {
					continue;
				}
				enableNote(noteCode + offset, velocity);
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
	if (shiftEnabled) {
		state.rootRows += offset;
		if (state.rootRows < 0) {
			state.rootRows = 0;
		}
		else if (state.rootRows > 4) {
			state.rootRows = 4;
		}
		precalculate();
		return;
	}

	if (encoderPressed) {
		for (int32_t idxPress = kMaxNumKeyboardPadPresses - 1; idxPress >= 0; --idxPress) {

			PressedPad pressed = presses[idxPress];
			if (pressed.active && pressed.x < kDisplayWidth) {

				int32_t chordNo = getChordNo(pressed.y);

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
		int32_t chordNo = getChordNo(y);
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

	bool inScaleMode = getScaleModeEnabled();

	// Precreate list of all scale notes per octave
	NoteSet octaveScaleNotes;
	if (inScaleMode) {
		NoteSet& scaleNotes = getScaleNotes();
		for (uint8_t idx = 0; idx < getScaleNoteCount(); ++idx) {
			octaveScaleNotes.add(scaleNotes[idx]);
		}
	}

	for (int32_t x = 0; x < kDisplayWidth; x++) {
		for (int32_t y = 0; y < kDisplayHeight; ++y) {
			int32_t chordNo = getChordNo(y);
			int32_t pageNo = std::min<int32_t>(chordNo / kDisplayHeight, kVerticalPages - 1);

			ChordQuality quality = state.chordList.chords[chordNo].quality;
			int32_t noteCode = noteFromCoords(x, y);

			uint16_t noteWithinScale = (uint16_t)((noteCode + kOctaveSize) - getRootNote()) % kOctaveSize;

			if (y < state.rootRows) {
				RGB colour =
				    noteColours[(x - ((state.rootRows + noteColours.size()) - y) * (getState().isomorphic.rowInterval))
				                % noteColours.size()];
				// if (octaveScaleNotes.has(noteWithinScale)) {
				if (noteWithinScale == 0) {
					image[y][x] = colour.dim(1);
				}
				else {
					image[y][x] = colours::black;
				}

				// if ((y == state.rootRows - 1) || (y == 0)) {
				// 	image[y][x] = noteColours[x % noteColours.size()].dim(4);
				// }
				// else {
				// 	image[y][x] = colours::black;
				// }
				continue;
			}

			// even if not in scale mode we still have a root defined
			if (inScaleMode) {
				NoteSet& scaleNotes = getScaleNotes();

				// D_PRINTLN("noteCode %d, noteWithinScale %d", noteCode, noteWithinScale);

				NoteSet intervalSet = state.chordList.chords[chordNo].intervalSet;

				// for (int i = 0; i < scaleNotes.count(); i++) {
				// 	D_PRINTLN("scaleNotes %d", scaleNotes[i]);
				// }
				// for (int i = 0; i < intervalSet.count(); i++) {
				// 	D_PRINTLN("intervalSet %d", intervalSet[i]);
				// }

				// D_PRINTLN("Scale Notes %d char %s", scaleNotes, scaleNotes);
				// D_PRINTLN("intervalSet %d, char %s", intervalSet, intervalSet);
				// for (int i = 0; i < noteWithinScale; i++) {
				NoteSet newNoteSet = intervalSet.toOffset(noteWithinScale);

				// for (int i = 0; i < newNoteSet.count(); i++) {
				// 	// D_PRINTLN("newNoteSet %d", newNoteSet[i]);
				// }
				// D_PRINTLN("newNotelSet %d, char %s", newNoteSet, newNoteSet);
				// }
				// if in scale, color the column
				if (newNoteSet.isSubsetOf(scaleNotes)) {
					// if (scaleNotes.has(noteWithinScale)) {
					// image[y][x] = noteColours[y];
					// image[y][x] = noteColours[x % noteColours.size()];
					image[y][x] = qualityColours[quality];
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
				else if (scaleNotes.has(noteWithinScale)) {
					// image[y][x] = noteColours[x % noteColours.size()].dim(4);
					image[y][x] = qualityColours[quality].dim(2);
				}
				// color the column the page color
				else {
					// image[y][x] = noteColours[x % noteColours.size()].dim(4);
					image[y][x] = pageColours[pageNo].dim(4);
				}
			}
			// if not in scale, color the entire column
			else {
				// image[y][x] = noteColours[x % noteColours.size()];
				image[y][x] = qualityColours[quality];
			}

			// We add a colored page color rows to the top and bottom of a page to help with navigation
			// And to the 2nd to last column (2nd to last col will be overwritten in scale mode)
			// // We also use different colors for each page
			// if ((x == kDisplayWidth - 2)               // show we've reached the start of a new page
			//     || (chordNo % 8 == 0)                  // show we've reached the start of a new page
			//     || (chordNo % 8 == kDisplayHeight - 1) // show we've reached the end of a page
			//     || (chordNo == kUniqueChords - 1)      // show we've reach the very top
			// )
			// if (
			// 	(x == kDisplayWidth - 2)               // show we've reached the start of a new page
			// 	|| (x == kDisplayWidth - 3)               // show we've reached the start of a new page
			//     // || (chordNo % 8 == 0)                  // show we've reached the start of a new page
			//     // || (chordNo % 8 == kDisplayHeight - 1) // show we've reached the end of a page
			//     // || (chordNo == kUniqueChords - 1)      // show we've reach the very top
			// ) {
			// 	image[y][x] = pageColours[pageNo].dim(7);
			// }
			// else if (x == kDisplayWidth - 4) {
			// 	image[y][x] = colours::black;
			// }
			// // We add chord quality to the rightmost column, we also do this last so it overwrites the other colors
			// if (x == kDisplayWidth - 1) {
			// 	image[y][x] = padQualityColours[y];
			// }
			// if (y == kDisplayHeight - 1) {
			// 	// 	image[y][x] = padQualityColours[y];
			// 	image[y][x] = noteColours[x % noteColours.size()].dim(4);
			// }

			// if (y == kDisplayHeight - 1) {
			// 	// 	image[y][x] = padQualityColours[y];
			// 	image[y][x] = noteColours[x % noteColours.size()].dim(4);
			// }
		}
	}
}

int32_t KeyboardLayoutChord::noteFromCoords(int32_t x, int32_t y) {
	KeyboardStateChord& state = getState().chord;
	if (y < state.rootRows) {
		return (getState().chord.noteOffset + x) - (((state.rootRows - 1) - y) * (getState().isomorphic.rowInterval));

		// ((state.rootRows - y) * (12 - getState().isomorphic.rowInterval));
	}
	return getState().chord.noteOffset + x;
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
