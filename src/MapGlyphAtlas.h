#pragma once

#include "imgui/imgui.h"
#include "vendor/stb_truetype.h"

#include <functional>
#include <cstdint>
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
	enum class Preparation { Ready, Pending, Failed };

	void setSources(std::vector<FontData> data);
	bool supports(ImWchar character);
	bool addText(const std::string& text);
	bool stage(ImWchar character);
	Preparation prepare(const std::set<Profile>& profiles, const Upload& upload, size_t pageBudget = SIZE_MAX);
	ImFont* find(float size, bool animation, ImWchar character) const;
	ImFont* findPrevious(float size, bool animation, ImWchar character) const;
	void clear();
	size_t sourceCount() const { return sources.size(); }
	size_t pageCount() const;
	size_t textureBytes() const;
	size_t buildCount() const { return builds; }
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
	struct Source {
		FontData data;
		stbtt_fontinfo info{};
	};
	std::vector<Source> sources;
	std::map<ImWchar, bool> coverage;
	std::set<ImWchar> characters;
	std::set<ImWchar> staged;
	std::map<Profile, Fonts> fonts;
	std::string lastError;
	bool dirty = true;
	size_t builds = 0;
	std::set<Profile> preparedProfiles;
};
