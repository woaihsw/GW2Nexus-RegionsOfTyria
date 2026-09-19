#include "MapFontService.h"
#include "../Globals.h"
#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>
#include <climits>
#include <iterator>

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

void MapFontService::initialize(const std::string& folder) {
	clear();
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
	hasSource = !sources.empty();
	hasAnimationFace = sources.size() > 1;
	atlas.setSources(std::move(sources));
	std::ifstream seed(folder + "/" + CJK_SEED_FILE, std::ios::binary);
	addText(std::string(std::istreambuf_iterator<char>(seed), std::istreambuf_iterator<char>()));
	addText("泰瑞亚 科瑞塔 女王谷 狮子拱门 红方 蓝方 绿方");
}

bool MapFontService::prepare() {
	// Preserve Latin-only installations where Windows has no CJK font package.
	if (!hasSource && settings.locale != Locale::Zh) {
		prepared = true;
		return true;
	}
	const auto now = std::chrono::steady_clock::now();
	if (now < retryAt) return false;
	const float scale = ImGui::GetIO().FontGlobalScale;
	std::set<MapGlyphAtlas::Profile> profiles;
	for (const auto& font : settings.fontSettings) {
		// One rasterization per face/size, even when all six races use it.
		for (float size : {font.smallFontSize, font.largeFontSize, font.widgetFontSize})
			profiles.insert({(size > 0 ? size : 10) * scale, false});
		if (!settings.disableAnimations && hasAnimationFace) {
			for (float size : {font.smallFontSize, font.largeFontSize})
				profiles.insert({(size > 0 ? size : 10) * scale, true});
		}
		// User-written template literals are part of the displayed text too.
		addText(font.displayFormatSmall + font.displayFormatLarge + font.widgetDisplayFormat);
	}
	const size_t before = atlas.pageCount();
	prepared = atlas.prepare(profiles, uploadPage);
	if (!prepared) {
		retryAt = now + std::chrono::seconds(5);
		APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, ("Map fonts pending: " + atlas.error()).c_str());
		return false;
	}
	if (before != atlas.pageCount()) {
		const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - now).count();
		std::ostringstream message;
		message << "Private map fonts: " << atlas.pageCount() << " pages, "
			<< atlas.textureBytes() / 1024 << " KiB textures, preparation " << ms << " ms; no Nexus atlas rebuild.";
		APIDefs->Log(ELogLevel_INFO, ADDON_NAME, message.str().c_str());
	}
	return true;
}

ImFont* MapFontService::find(float size, bool animation, ImWchar character) const {
	ImFont* font = atlas.find(size, animation && hasAnimationFace, character);
	return font ? font : atlas.find(size, false, character);
}

void MapFontService::clear() {
	atlas.clear();
	hasSource = hasAnimationFace = false;
	prepared = false;
	retryAt = {};
}
