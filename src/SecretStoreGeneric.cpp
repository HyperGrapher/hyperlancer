#include "SecretStore.h"

#include <utility>

#include <QFile>
#include <QSettings>

// Development fallback for Linux and macOS: an INI file readable only by the current user.
// Not encrypted. The app is meant to run on Windows, where SecretStoreWin.cpp is used.

SecretStore::SecretStore(QString fallbackFile)
    : m_fallbackFile(std::move(fallbackFile))
{}

bool SecretStore::write(const QString &key, const QString &value)
{
    {
        QSettings file(m_fallbackFile, QSettings::IniFormat);
        file.setValue(key, value);
        file.sync();
        if (file.status() != QSettings::NoError) {
            return false;
        }
    }
    QFile::setPermissions(m_fallbackFile, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return true;
}

std::optional<QString> SecretStore::read(const QString &key) const
{
    const QSettings file(m_fallbackFile, QSettings::IniFormat);
    if (!file.contains(key)) {
        return std::nullopt;
    }
    return file.value(key).toString();
}

bool SecretStore::remove(const QString &key)
{
    QSettings file(m_fallbackFile, QSettings::IniFormat);
    const bool existed = file.contains(key);
    file.remove(key);
    return existed;
}

QString SecretStore::backendName()
{
    return QStringLiteral("local file (development only, not encrypted)");
}

bool SecretStore::isSecureBackend()
{
    return false;
}
