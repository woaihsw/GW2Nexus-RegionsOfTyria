#include "MapFontService.h"
#include "../Globals.h"
#include "AddonRenderer.h"
#include "../imgui/imgui_internal.h"
#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>
#include <climits>

using Microsoft::WRL::ComPtr;

MapFontService mapFonts;

static MapGlyphAtlas::Texture uploadPage(const unsigned char* pixels, int width, int height) {
	if (!APIDefs || !APIDefs->SwapChain) return {};
	ComPtr<ID3D11Device> device;
	auto* swapChain = static_cast<IDXGISwapChain*>(APIDefs->SwapChain);
	if (FAILED(swapChain->GetDevice(IID_PPV_ARGS(device.GetAddressOf())))) return {};
	D3D11_TEXTURE2D_DESC desc{};
	desc.Width = width;
	desc.Height = height;
	desc.MipLevels = desc.ArraySize = 1;
	desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_IMMUTABLE;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	D3D11_SUBRESOURCE_DATA data{};
	data.pSysMem = pixels;
	data.SysMemPitch = width * 4;
	ComPtr<ID3D11Texture2D> texture;
	if (FAILED(device->CreateTexture2D(&desc, &data, texture.GetAddressOf()))) return {};
	ID3D11ShaderResourceView* view = nullptr;
	if (FAILED(device->CreateShaderResourceView(texture.Get(), nullptr, &view))) return {};
	return {view, [](void* value) { static_cast<ID3D11ShaderResourceView*>(value)->Release(); }};
}

void MapFontService::initialize() {
	wchar_t windowsDir[MAX_PATH]{};
	const UINT length = GetWindowsDirectoryW(windowsDir, MAX_PATH);
	const fs::path fontsDir = (length > 0 && length < MAX_PATH ? fs::path(windowsDir) : fs::path(L"C:/Windows")) / "Fonts";
	std::vector<MapGlyphAtlas::FontData> sources;
	for (const char* filename : {"msyh.ttc", "simsun.ttc", "simhei.ttf", "Deng.ttf", "msjh.ttc", "mingliu.ttc"}) {
		std::ifstream file(fontsDir / filename, std::ios::binary | std::ios::ate);
		if (!file) continue;
		const std::streamoff length = file.tellg();
		if (length <= 0 || length > INT_MAX) continue;
		auto bytes = std::make_shared<std::vector<unsigned char>>(static_cast<size_t>(length));
		file.seekg(0);
		if (!file.read(reinterpret_cast<char*>(bytes->data()), length)) continue;
		sources.push_back(std::move(bytes));
		if (sources.size() == 2) break;
	}
	setSources(std::move(sources));
	addText("泰瑞亚 科瑞塔 女王谷 狮子拱门 红方 蓝方 绿方");
}

bool MapFontService::prepare() {
	const auto now = Clock::now();
	const float scale = ImGui::GetIO().FontGlobalScale;
	std::set<MapGlyphAtlas::Profile> profiles;
	std::string literals;
	for (const auto& font : settings.fontSettings) {
		for (float size : {font.smallFontSize, font.largeFontSize, font.widgetFontSize})
			profiles.insert({(size > 0 ? size : 10) * scale, false});
		if (!settings.disableAnimations && hasAnimationFace()) {
			for (float size : {font.smallFontSize, font.largeFontSize})
				profiles.insert({(size > 0 ? size : 10) * scale, true});
		}
		literals += font.displayFormatSmall + font.displayFormatLarge + font.widgetDisplayFormat;
	}
	const size_t before = atlas.pageCount();
	const bool attempted = now >= retryAt || profiles != requestedProfiles;
	const bool complete = prepare(profiles, literals,
		[](const MapGlyphAtlas::Profile& profile, ImWchar c) { return renderer.hostGlyphAvailable(profile.size, profile.animation, c); },
		renderer.fontRevision(), uploadPage, now);
	for (ImWchar c : missing) {
		if (!reportedMissing.insert(c).second) continue;
		char message[160];
		ImFormatString(message, sizeof(message), "Map fonts lack U+%04X; only names requiring this glyph remain pending.", static_cast<unsigned>(c));
		APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, message);
	}
	if (attempted && !complete && !atlas.error().empty())
		APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, ("Map font upload pending: " + atlas.error()).c_str());
	if (before != atlas.pageCount()) {
		const double ms = std::chrono::duration<double, std::milli>(Clock::now() - now).count();
		std::ostringstream message;
		message << "Private map fonts: " << atlas.pageCount() << " pages, "
			<< atlas.textureBytes() / 1024 << " KiB textures, preparation " << ms << " ms; no Nexus atlas rebuild.";
		APIDefs->Log(ELogLevel_INFO, ADDON_NAME, message.str().c_str());
	}
	return complete;
}
