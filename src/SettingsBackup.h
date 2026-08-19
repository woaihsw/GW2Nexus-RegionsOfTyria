#ifndef SETTINGS_BACKUP_H
#define SETTINGS_BACKUP_H

#include <functional>
#include <string>

inline std::string settingsBackupCandidate(const std::string& settingsPath, int index) {
	const std::string preferred = settingsPath + ".bad";
	if (index <= 0) {
		return preferred;
	}
	return preferred + "." + std::to_string(index);
}

inline std::string nextSettingsBackupPath(
	const std::string& settingsPath,
	const std::function<bool(const std::string&)>& exists) {
	for (int index = 0; index < 10000; ++index) {
		const std::string candidate = settingsBackupCandidate(settingsPath, index);
		if (!exists(candidate)) {
			return candidate;
		}
	}
	return settingsBackupCandidate(settingsPath, 0);
}

#endif
