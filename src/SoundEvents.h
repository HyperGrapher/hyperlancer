#pragma once

#include <array>

#include <QString>

// The moments the app can play a sound for. The user picks a sound per event in Settings;
// "defaultSound" is what an event plays until they change it. Sound names are the file names
// in resources/sounds without the extension.
namespace SoundEvents {

struct Event
{
    const char *id;
    const char *label;
    const char *defaultSound;
};

inline constexpr std::array<Event, 8> kEvents{{
    {"mailArrived", "New email", "message"},
    {"analysisDone", "Analysis finished", "ding"},
    {"analysisFailed", "Analysis failed", "error"},
    {"speedCheckDone", "Speed check finished", "ding"},
    {"crawlDone", "Content crawl finished", "ding"},
    {"discoveryDone", "Discovery finished", "idea"},
    {"visitorArrived", "New website visitor", "project"},
    {"error", "Other errors", "error"},
}};

[[nodiscard]] inline QString defaultSoundFor(const QString &eventId)
{
    for (const Event &event : kEvents) {
        if (eventId == QLatin1String(event.id)) {
            return QString::fromLatin1(event.defaultSound);
        }
    }
    return {};
}

} // namespace SoundEvents
