/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef PEGASUS_SUBTITLES_H
#define PEGASUS_SUBTITLES_H

#include "common/array.h"
#include "common/path.h"

namespace Video {
class SRTParser;
class Subtitles;
class VideoDecoder;
}

namespace Pegasus {

/**
 * Third-party subtitles for Pegasus Prime (enhancement, not in the original game).
 *
 * For a media file "Images/AI/Caldoria/XA01" (or "Sounds/Caldoria/Foo.aiff"), the
 * subtitles are read from "Subtitles/Images/AI/Caldoria/XA01.srt" in the game folder.
 * Timestamps are relative to the start of the media file, so that segments of the
 * large navigation movies stay in sync.
 *
 * Anything that plays a media file registers itself as a SubtitleSource; the manager
 * is ticked once per frame and draws the subtitle of the most recently started
 * active source on the ScummVM overlay.
 */

enum SubtitlePlacement {
	kSubtitlesInView,       // bottom of the 512x256 navigation view
	kSubtitlesFullScreen    // bottom of the 640x480 screen (intro, closing...)
};

class SubtitleSource {
public:
	virtual ~SubtitleSource() {}
	/** Is the media currently playing? */
	virtual bool isSubtitleSourceActive() const = 0;
	/** Current position in the media file, in milliseconds. */
	virtual uint32 getSubtitleSourceTime() const = 0;
};

class SubtitleManager {
public:
	SubtitleManager();
	~SubtitleManager();

	/** Load the subtitles matching mediaPath (if any) for this source. */
	void attach(SubtitleSource *source, const Common::Path &mediaPath, SubtitlePlacement placement = kSubtitlesInView);
	/** Forget a source (must be called before it is destroyed). */
	void detach(SubtitleSource *source);
	/** Mark a source as just (re)started: it gets priority over the others. */
	void started(SubtitleSource *source);

	/** Draw / update / hide subtitles. Call once per frame. */
	void update();

	bool isEnabled() const { return _enabled; }

private:
	struct Entry {
		SubtitleSource *source;
		Video::Subtitles *subs;
		Video::SRTParser *parser;   // to know which source has a line to show right now
		SubtitlePlacement placement;
		uint32 order;
	};

	Entry *find(SubtitleSource *source);
	void layout(Entry &entry, bool force);
	void hide();

	Common::Array<Entry> _entries;
	uint32 _counter;
	bool _enabled;
	const Video::Subtitles *_shown;  // subtitles currently on the overlay
	int16 _overlayW, _overlayH;
};

/** The engine's subtitle manager, or nullptr if the engine is not running. */
SubtitleManager *getSubtitleManager();

/** Helper for code paths that play a raw Video::VideoDecoder in their own loop. */
class DecoderSubtitleSource : public SubtitleSource {
public:
	DecoderSubtitleSource(Video::VideoDecoder *video, const Common::Path &mediaPath, SubtitlePlacement placement = kSubtitlesFullScreen);
	~DecoderSubtitleSource() override;

	bool isSubtitleSourceActive() const override;
	uint32 getSubtitleSourceTime() const override;

private:
	Video::VideoDecoder *_video;
};

} // End of namespace Pegasus

#endif
