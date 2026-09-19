#include "MapFontService.h"
#include "../MapGlyphPolicy.h"

void MapFontService::setSources(std::vector<MapGlyphAtlas::FontData> sources) {
	clear();
	atlas.setSources(std::move(sources));
}

void MapFontService::addText(const std::string& text) {
	for (ImWchar c : mapTextCharacters(text)) dirty = observed.insert(c).second || dirty;
}

bool MapFontService::prepare(const std::set<MapGlyphAtlas::Profile>& profiles, const std::string& literals,
	HostCoverage host, uint64_t hostRevision, const MapGlyphAtlas::Upload& upload,
	Clock::time_point now, size_t pageBudget) {
	hostCoverage = std::move(host);
	if (profiles != requestedProfiles) retryAt = {};
	if (dirty || !configured || profiles != requestedProfiles || literals != templateText || hostRevision != lastHostRevision) {
		requestedProfiles = profiles;
		templateText = literals;
		lastHostRevision = hostRevision;
		missing.clear();
		auto required = observed;
		const auto templateCharacters = mapTextCharacters(literals);
		required.insert(templateCharacters.begin(), templateCharacters.end());
		for (ImWchar c : required) {
			bool needsPrivate = needsNativeMapGlyph(c);
			if (!needsPrivate) {
				for (const auto& profile : profiles) {
					if (!hostCoverage || !hostCoverage(profile, c)) { needsPrivate = true; break; }
				}
			}
			if (needsPrivate && !atlas.stage(c)) missing.insert(c);
		}
		dirty = false;
		configured = true;
	}
	if (now < retryAt) return false;
	const auto result = atlas.prepare(profiles, upload, pageBudget);
	if (result == MapGlyphAtlas::Preparation::Failed) retryAt = now + std::chrono::seconds(5);
	if (result == MapGlyphAtlas::Preparation::Ready) prepared = true;
	// Unsupported characters never enter the atlas's staged/committed sets.
	// Individual maps decide readiness below, independently of this attempt.
	return result == MapGlyphAtlas::Preparation::Ready;
}

bool MapFontService::textReady(const std::string& text) const {
	if (!configured || requestedProfiles.empty()) return false;
	for (ImWchar c : mapTextCharacters(text)) {
		for (const auto& profile : requestedProfiles) {
			if (!needsNativeMapGlyph(c) && hostCoverage && hostCoverage(profile, c)) continue;
			if (!atlas.find(profile.size, profile.animation, c)) return false;
		}
	}
	return true;
}

ImFont* MapFontService::find(float size, bool animation, ImWchar character) const {
	animation = animation && hasAnimationFace();
	if (ImFont* font = atlas.find(size, animation, character)) return font;
	if (ImFont* font = atlas.findPrevious(size, animation, character)) return font;
	if (ImFont* font = atlas.find(size, false, character)) return font;
	return atlas.findPrevious(size, false, character);
}

void MapFontService::clear() {
	atlas.clear();
	observed.clear();
	missing.clear();
	reportedMissing.clear();
	requestedProfiles.clear();
	templateText.clear();
	hostCoverage = {};
	lastHostRevision = 0;
	dirty = true;
	configured = prepared = false;
	retryAt = {};
}
