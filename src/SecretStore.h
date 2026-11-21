#pragma once

#include <optional>

#include <QString>

// API keys, tokens and passwords. On Windows they go to the Windows Credential Manager
// (Control Panel > Credential Manager > Windows Credentials, entries named
// "Hyperlancer/<key>"), encrypted for the signed-in user. Other platforms are only used for
// development and keep them in a user-only file next to the settings.
class SecretStore
{
public:
    // fallbackFile is used only where no OS store is wired in.
    explicit SecretStore(QString fallbackFile);

    [[nodiscard]] bool write(const QString &key, const QString &value);
    [[nodiscard]] std::optional<QString> read(const QString &key) const;
    bool remove(const QString &key);

    // "Windows Credential Manager" or a description of the fallback.
    [[nodiscard]] static QString backendName();
    [[nodiscard]] static bool isSecureBackend();

private:
    QString m_fallbackFile;
};

// Key names used across the app, so they are spelled once.
namespace SecretKeys {
inline const QString kLangSearchApiKey = QStringLiteral("langsearch/api-key");
inline const QString kGmailClientId = QStringLiteral("gmail/client-id");
inline const QString kGmailClientSecret = QStringLiteral("gmail/client-secret");
inline const QString kGmailRefreshToken = QStringLiteral("gmail/refresh-token");
// The second mailbox has its own OAuth client: a personal Gmail account cannot use a client
// that belongs to a Google Workspace organisation (error org_internal).
inline const QString kGmailClientId2 = QStringLiteral("gmail/client-id-2");
inline const QString kGmailClientSecret2 = QStringLiteral("gmail/client-secret-2");
inline const QString kGmailRefreshToken2 = QStringLiteral("gmail/refresh-token-2");
inline const QString kConvexAdminKey = QStringLiteral("convex/admin-key");
inline const QString kMySqlPassword = QStringLiteral("mysql/password");
} // namespace SecretKeys
