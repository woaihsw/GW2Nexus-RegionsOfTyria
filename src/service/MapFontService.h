#pragma once

#include "../MapGlyphAtlas.h"
#include <chrono>
#include <cstdint>

class MapFontService {
public:
	using Clock = std::chrono::steady_clock;
	using HostCoverage = std::function<bool(const MapGlyphAtlas::Profile&, ImWchar)>;
	void initialize();
	void setSources(std::vector<MapGlyphAtlas::FontData> sources);
	void addText(const std::string& text);
	bool prepare();
	// Portable core used by both the Nexus adapter and real rasterization tests.
	bool prepare(const std::set<MapGlyphAtlas::Profile>& profiles, const std::string& literals,
		HostCoverage host, uint64_t hostRevision, const MapGlyphAtlas::Upload& upload,
		Clock::time_point now = Clock::now(), size_t pageBudget = 1);
	bool textReady(const std::string& text) const;
	ImFont* find(float size, bool animation, ImWchar character) const;
	bool ready() const { return prepared; }
	void clear();
	size_t pageCount() const { return atlas.pageCount(); }
	size_t textureBytes() const { return atlas.textureBytes(); }
	size_t buildCount() const { return atlas.buildCount(); }
	const std::set<ImWchar>& missingCharacters() const { return missing; }
	const std::string& error() const { return atlas.error(); }
private:
	MapGlyphAtlas atlas;
	std::set<ImWchar> observed, missing, reportedMissing;
	std::set<MapGlyphAtlas::Profile> requestedProfiles;
	std::string templateText;
	HostCoverage hostCoverage;
	uint64_t lastHostRevision = 0;
	bool dirty = true;
	bool configured = false;
	bool hasAnimationFace = false;
	bool prepared = false;
	Clock::time_point retryAt{};
};

extern MapFontService mapFonts;
