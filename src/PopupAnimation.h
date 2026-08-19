#ifndef POPUP_ANIMATION_H
#define POPUP_ANIMATION_H

struct PopupAnimationParams {
	float stepDelaySeconds = 0.035f;
	float holdSeconds = 3.0f;
	float opacityStep = 0.05f;

	static PopupAnimationParams fromSettings(int speedMs, int holdSecondsValue) {
		PopupAnimationParams params;
		params.stepDelaySeconds = speedMs > 0 ? static_cast<float>(speedMs) / 1000.0f : 0.0f;
		params.holdSeconds = holdSecondsValue > 0 ? static_cast<float>(holdSecondsValue) : 0.0f;
		return params;
	}

	float fadeSeconds() const {
		if (stepDelaySeconds <= 0.0f || opacityStep <= 0.0f) {
			return 0.0f;
		}
		return (1.0f / opacityStep) * stepDelaySeconds;
	}

	float totalSeconds() const {
		return fadeSeconds() * 2.0f + holdSeconds;
	}
};

inline float PopupOpacityAt(float elapsedSeconds, const PopupAnimationParams& params) {
	if (elapsedSeconds < 0.0f) {
		return 0.0f;
	}

	const float fade = params.fadeSeconds();
	if (fade <= 0.0f) {
		return elapsedSeconds < params.holdSeconds ? 1.0f : 0.0f;
	}
	if (elapsedSeconds < fade) {
		return elapsedSeconds / fade;
	}
	if (elapsedSeconds < fade + params.holdSeconds) {
		return 1.0f;
	}

	const float fadeOutElapsed = elapsedSeconds - fade - params.holdSeconds;
	if (fadeOutElapsed < fade) {
		return 1.0f - fadeOutElapsed / fade;
	}
	return 0.0f;
}

inline bool PopupAnimationActive(float elapsedSeconds, const PopupAnimationParams& params) {
	return elapsedSeconds >= 0.0f && elapsedSeconds < params.totalSeconds();
}

#endif
