#pragma once

#include "imgui/imgui.h"

#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

// Render-thread owned. Pages are immutable after upload; adding glyphs cannot
// invalidate fonts or UVs already used by another page (or by Nexus).
class MapGlyphAtlas {
public:
	struct Profile {
		float size;
		bool animation = false;
		bool operator==(const Profile&) const = default;
		bool operator<(const Profile& other) const {
			return size < other.size || (size == other.size && animation < other.animation);
		}
	};
	using FontData = std::shared_ptr<std::vector<unsigned char>>;
	using Texture = std::shared_ptr<void>;
	using Upload = std::function<Texture(const unsigned char*, int, int)>;

	void setSources(std::vector<FontData> data);
	void addText(const std::string& text);
	bool prepare(const std::set<Profile>& profiles, const Upload& upload);
	ImFont* find(float size, bool animation, ImWchar character) const;
	void clear();
	size_t pageCount() const;
	size_t textureBytes() const;
	const std::string& error() const { return lastError; }

private:
	struct Page {
		std::unique_ptr<ImFontAtlas> atlas;
		Texture texture;
		size_t bytes = 0;
	};
	struct Fonts {
		std::map<ImWchar, ImFont*> glyphs;
		std::vector<Page> pages;
	};
	std::vector<FontData> sources;
	std::set<ImWchar> characters;
	std::map<Profile, Fonts> fonts;
	std::string lastError;
	bool dirty = true;
	std::set<Profile> preparedProfiles;
};
