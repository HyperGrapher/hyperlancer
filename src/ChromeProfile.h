#pragma once

#include <QString>
#include <QUrl>

// Opens a link in a named Google Chrome profile (the name shown in Chrome's profile menu).
namespace ChromeProfile {

// The profile directory ("Default", "Profile 2") for a display name, read from Chrome's
// "Local State" file. Empty when no profile has that name.
[[nodiscard]] QString directoryForName(const QByteArray &localStateJson, const QString &name);

// Starts Chrome for the profile. Returns false when Chrome or the profile is not found, so
// the caller can fall back to the default browser.
[[nodiscard]] bool open(const QUrl &url, const QString &profileName);

} // namespace ChromeProfile
