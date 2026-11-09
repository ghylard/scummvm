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

#include "common/config-manager.h"
#include "common/debug.h"
#include "common/file.h"
#include "common/fs.h"
#include "common/archive.h"
#include "common/system.h"

#include "video/subtitles.h"
#include "video/video_decoder.h"

#include "pegasus/subtitles.h"

namespace Pegasus {

// Original game screen
static const int kScreenW = 640;
static const int kScreenH = 480;

// Navigation view (see constants.h: kNavAreaLeft/Top, 512x256)
static const int kViewLeft = 64;
static const int kViewTop = 64;
static const int kViewRight = 64 + 512;
static const int kViewBottom = 64 + 256;

static const char *kSubtitlesArchive = "pegasus-subtitles";

SubtitleManager::SubtitleManager() : _counter(0), _shown(nullptr), _overlayW(-1), _overlayH(-1) {
	// Enabled unless the player explicitly unticked "Subtitles" in the options
	_enabled = !ConfMan.hasKey("subtitles") || ConfMan.getBool("subtitles");

	// The game folder is only searched 4 levels deep, but subtitles of the AI clips live in
	// Subtitles/Images/AI/<zone>/ (5 levels): register the Subtitles folder on its own.
	Common::FSNode subtitlesDir = Common::FSNode(ConfMan.getPath("path")).getChild("Subtitles");
	if (subtitlesDir.exists() && subtitlesDir.isDirectory())
		SearchMan.addDirectory(kSubtitlesArchive, subtitlesDir, 0, 10);
}

SubtitleManager::~SubtitleManager() {
	hide();
	SearchMan.remove(kSubtitlesArchive);
	for (uint i = 0; i < _entries.size(); i++) {
		delete _entries[i].subs;
		delete _entries[i].parser;
	}
}

SubtitleManager::Entry *SubtitleManager::find(SubtitleSource *source) {
	for (uint i = 0; i < _entries.size(); i++)
		if (_entries[i].source == source)
			return &_entries[i];
	return nullptr;
}

void SubtitleManager::attach(SubtitleSource *source, const Common::Path &mediaPath, SubtitlePlacement placement) {
	detach(source);

	if (!_enabled || mediaPath.empty())
		return;

	// Same rule as Movie::initFromMovieFile(): ':' cannot be used outside macOS
	Common::String name = mediaPath.toString('/');
	for (uint i = 0; i < name.size(); i++)
		if (name[i] == ':')
			name.setChar('_', i);

	// Path relative to the "Subtitles" folder registered in the constructor
	Common::Path srtPath = Common::Path(name).append(".srt");

	if (!Common::File::exists(srtPath))
		return;

	Video::Subtitles *subs = new Video::Subtitles();
	subs->loadSRTFile(srtPath);

	if (!subs->isLoaded()) {
		warning("Could not parse subtitles '%s'", srtPath.toString().c_str());
		delete subs;
		return;
	}

	debug(1, "Pegasus: subtitles '%s' attached", srtPath.toString().c_str());

	Video::SRTParser *parser = new Video::SRTParser();
	parser->parseFile(srtPath);

	Entry entry;
	entry.source = source;
	entry.subs = subs;
	entry.parser = parser;
	entry.placement = placement;
	entry.order = ++_counter;
	entry.lines = 2;
	entry.fontSize = 0;
	_entries.push_back(entry);
	layout(_entries.back(), 2);
}

void SubtitleManager::detach(SubtitleSource *source) {
	for (uint i = 0; i < _entries.size(); i++) {
		if (_entries[i].source == source) {
			if (_shown == _entries[i].subs)
				hide();
			delete _entries[i].subs;
			delete _entries[i].parser;
			_entries.remove_at(i);
			return;
		}
	}
}

void SubtitleManager::started(SubtitleSource *source) {
	Entry *entry = find(source);
	if (entry)
		entry->order = ++_counter;
}

void SubtitleManager::layout(Entry &entry, int lines) {
	int16 w = g_system->getOverlayWidth();
	int16 h = g_system->getOverlayHeight();

	// The 640x480 game screen is shown with its aspect ratio kept, centered in the overlay
	float scale = MIN<float>((float)w / kScreenW, (float)h / kScreenH);
	int offX = (int)((w - kScreenW * scale) / 2);
	int offY = (int)((h - kScreenH * scale) / 2);

	// The text is drawn from the top of the box: size the box to the number of lines
	// of the current subtitle so that the text always hugs the bottom of the area.
	// TrueType fonts are loaded at 96 dpi, so a line is about 1.5x the point size.
	const int kFontSize = 12;
	const int kLineHeight = kFontSize * 3 / 2 + 1;
	int boxHeight = MAX(1, lines) * kLineHeight + 6;

	Common::Rect r;
	if (entry.placement == kSubtitlesInView) {
		// Bottom of the navigation view, so that the AI and inventory areas stay visible
		r = Common::Rect(kViewLeft + 8, kViewBottom - 4 - boxHeight, kViewRight - 8, kViewBottom - 4);
	} else {
		r = Common::Rect(24, kScreenH - 16 - boxHeight, kScreenW - 24, kScreenH - 16);
	}

	Common::Rect bbox(offX + (int)(r.left * scale), offY + (int)(r.top * scale),
	                  offX + (int)(r.right * scale), offY + (int)(r.bottom * scale));

	int fontSize = MAX<int>(9, (int)(kFontSize * scale));

	entry.lines = lines;
	entry.subs->setBBox(bbox);
	entry.subs->setColor(0xff, 0xff, 0xff);
	entry.subs->setPadding(4, 2);

	// Loading a TrueType font is not free: only do it when the size changes
	if (fontSize != entry.fontSize) {
		entry.fontSize = fontSize;
		entry.subs->setFont("LiberationSans-Regular.ttf", fontSize, Video::Subtitles::kFontStyleRegular);
		entry.subs->setFont("LiberationSans-Italic.ttf", fontSize, Video::Subtitles::kFontStyleItalic);
	}
}

static int countLines(const Common::Array<Video::SubtitlePart> &parts) {
	int lines = 1;
	for (uint i = 0; i < parts.size(); i++)
		for (uint j = 0; j < parts[i].text.size(); j++)
			if (parts[i].text[j] == '\n')
				lines++;
	return lines;
}

void SubtitleManager::hide() {
	if (_shown) {
		_shown->clearSubtitle();
		_shown = nullptr;
	}
}

void SubtitleManager::update() {
	if (_entries.empty()) {
		hide();
		return;
	}

	int16 w = g_system->getOverlayWidth();
	int16 h = g_system->getOverlayHeight();
	bool resized = (w != _overlayW || h != _overlayH);
	_overlayW = w;
	_overlayH = h;

	// Most recently started source that is playing AND has a line at its current time
	Entry *best = nullptr;
	uint32 bestTime = 0;
	const Common::Array<Video::SubtitlePart> *bestParts = nullptr;
	for (uint i = 0; i < _entries.size(); i++) {
		Entry &e = _entries[i];
		if (resized)
			layout(e, e.lines);
		if (!e.source->isSubtitleSourceActive() || (best && e.order < best->order))
			continue;
		uint32 time = e.source->getSubtitleSourceTime();
		const Common::Array<Video::SubtitlePart> *parts = e.parser->getSubtitleParts(time);
		if (parts && !parts->empty()) {
			best = &e;
			bestTime = time;
			bestParts = parts;
		}
	}

	// Nothing to say: no overlay at all (on backends without an alpha
	// overlay, an empty overlay would freeze the game picture)
	if (!best) {
		hide();
		return;
	}

	bool force = resized;

	int lines = countLines(*bestParts);
	if (lines != best->lines) {
		// The box changes size: wipe what the previous (larger) box left on the overlay
		if (_shown == best->subs)
			hide();
		layout(*best, lines);
		force = true;
	}

	if (_shown != best->subs) {
		hide();
		force = true;
	}

	// The global main menu (or anything else) may have taken the overlay away
	if (!g_system->isOverlayVisible())
		force = true;

	best->subs->drawSubtitle(bestTime, force);
	_shown = best->subs;
}

// DecoderSubtitleSource

DecoderSubtitleSource::DecoderSubtitleSource(Video::VideoDecoder *video, const Common::Path &mediaPath, SubtitlePlacement placement) : _video(video) {
	SubtitleManager *manager = getSubtitleManager();
	if (manager) {
		manager->attach(this, mediaPath, placement);
		manager->started(this);
	}
}

DecoderSubtitleSource::~DecoderSubtitleSource() {
	SubtitleManager *manager = getSubtitleManager();
	if (manager)
		manager->detach(this);
}

bool DecoderSubtitleSource::isSubtitleSourceActive() const {
	return _video && _video->isPlaying() && !_video->endOfVideo();
}

uint32 DecoderSubtitleSource::getSubtitleSourceTime() const {
	return _video ? _video->getTime() : 0;
}

} // End of namespace Pegasus
