#pragma once

#include <map>
#include <memory>

#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

class AppSettings;
class QAudioOutput;
class QMediaPlayer;

// Plays the sound files in resources/sounds (MP3) for the events in SoundEvents.h. Which
// sound belongs to which event, the volume and the on/off switch live in AppSettings.
class SoundPlayer : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("SoundPlayer is created by the application.")

    // { value: file name without extension, label } for every bundled sound.
    Q_PROPERTY(QVariantList sounds READ sounds CONSTANT)
    // { id, label } for every event that can have a sound.
    Q_PROPERTY(QVariantList events READ events CONSTANT)

public:
    explicit SoundPlayer(const AppSettings &settings, QObject *parent = nullptr);
    ~SoundPlayer() override;

    [[nodiscard]] QVariantList sounds() const { return m_sounds; }
    [[nodiscard]] QVariantList events() const { return m_events; }

    // Plays the sound assigned to the event, if sounds are on and one is assigned. Events
    // that follow each other within a fraction of a second (a failure that also raises an
    // error notice) play once.
    Q_INVOKABLE void playEvent(const QString &eventId);
    // Plays one sound at the current volume, for the Settings page. Works even when sounds
    // are switched off, so a sound can be tried first.
    Q_INVOKABLE void preview(const QString &soundName);

private:
    struct Voice
    {
        std::unique_ptr<QMediaPlayer> player;
        std::unique_ptr<QAudioOutput> output;
    };

    void play(const QString &soundName);
    Voice &voiceFor(const QString &soundName);

    const AppSettings &m_settings;
    QVariantList m_sounds;
    QVariantList m_events;
    std::map<QString, Voice> m_voices; // created on first use, one per sound file
    QElapsedTimer m_sinceLastEvent;
};
