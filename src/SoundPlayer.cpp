#include "SoundPlayer.h"

#include <QAudioOutput>
#include <QDir>
#include <QMediaPlayer>
#include <QUrl>

#include "AppSettings.h"
#include "SoundEvents.h"

namespace {

constexpr qint64 kEventDebounceMs = 400;
const QString kSoundFolder = QStringLiteral(":/sounds");

QString labelFor(const QString &fileName)
{
    QString label = fileName;
    label.replace(QLatin1Char('-'), QLatin1Char(' ')).replace(QLatin1Char('_'), QLatin1Char(' '));
    return label.isEmpty() ? label : label.at(0).toUpper() + label.mid(1);
}

} // namespace

SoundPlayer::SoundPlayer(const AppSettings &settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
{
    const QStringList files = QDir(kSoundFolder).entryList({QStringLiteral("*.mp3")}, QDir::Files);
    for (const QString &file : files) {
        const QString name = QFileInfo(file).completeBaseName();
        m_sounds.append(QVariantMap{{QStringLiteral("value"), name}, {QStringLiteral("label"), labelFor(name)}});
    }
    for (const SoundEvents::Event &event : SoundEvents::kEvents) {
        m_events.append(QVariantMap{{QStringLiteral("id"), QString::fromLatin1(event.id)},
                                    {QStringLiteral("label"), tr(event.label)}});
    }
}

SoundPlayer::~SoundPlayer() = default;

SoundPlayer::Voice &SoundPlayer::voiceFor(const QString &soundName)
{
    const auto found = m_voices.find(soundName);
    if (found != m_voices.end()) {
        return found->second;
    }
    Voice voice;
    voice.output = std::make_unique<QAudioOutput>();
    voice.player = std::make_unique<QMediaPlayer>();
    voice.player->setAudioOutput(voice.output.get());
    voice.player->setSource(QUrl(QStringLiteral("qrc:/sounds/%1.mp3").arg(soundName)));
    return m_voices.emplace(soundName, std::move(voice)).first->second;
}

void SoundPlayer::play(const QString &soundName)
{
    if (soundName.isEmpty() || !QFile::exists(kSoundFolder + QLatin1Char('/') + soundName + QStringLiteral(".mp3"))) {
        return;
    }
    Voice &voice = voiceFor(soundName);
    voice.output->setVolume(static_cast<float>(m_settings.soundVolume()));
    voice.player->stop();
    voice.player->play();
}

void SoundPlayer::playEvent(const QString &eventId)
{
    if (!m_settings.soundsEnabled()) {
        return;
    }
    if (m_sinceLastEvent.isValid() && m_sinceLastEvent.elapsed() < kEventDebounceMs) {
        return;
    }
    const QString sound = m_settings.soundFor(eventId);
    if (sound.isEmpty()) {
        return;
    }
    m_sinceLastEvent.restart();
    play(sound);
}

void SoundPlayer::preview(const QString &soundName)
{
    play(soundName);
}
