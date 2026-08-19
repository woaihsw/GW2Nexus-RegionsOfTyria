#ifndef FONT_RELOAD_H
#define FONT_RELOAD_H

enum class FontReloadState {
	Idle,
	WaitingForRelease
};

struct FontReloadSchedule {
	FontReloadState state = FontReloadState::Idle;
	static constexpr float kGraceSeconds = 0.05f;

	void request() {
		state = FontReloadState::WaitingForRelease;
	}

	void cancel() {
		state = FontReloadState::Idle;
	}

	bool shouldLoadFonts(bool fontsCleared, float secondsSinceRequest) const {
		if (state != FontReloadState::WaitingForRelease) {
			return false;
		}
		if (!fontsCleared) {
			return false;
		}
		return secondsSinceRequest >= kGraceSeconds;
	}
};

#endif
