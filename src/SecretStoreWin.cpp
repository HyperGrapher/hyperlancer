#include "SecretStore.h"

#include <string>
#include <utility>

// windows.h must come first: wincred.h relies on its types. The blank line keeps
// clang-format from sorting them.
#include <windows.h>

#include <wincred.h>

namespace {

std::wstring targetName(const QString &key)
{
    return (QStringLiteral("Hyperlancer/") + key).toStdWString();
}

} // namespace

SecretStore::SecretStore(QString fallbackFile)
    : m_fallbackFile(std::move(fallbackFile))
{}

bool SecretStore::write(const QString &key, const QString &value)
{
    // CREDENTIALW takes mutable pointers, so keep writable copies instead of casting.
    std::wstring target = targetName(key);
    QByteArray blob = value.toUtf8();
    if (blob.size() > CRED_MAX_CREDENTIAL_BLOB_SIZE) {
        return false;
    }
    std::wstring user = L"Hyperlancer";
    CREDENTIALW credential{};
    credential.Type = CRED_TYPE_GENERIC;
    credential.TargetName = target.data();
    credential.CredentialBlobSize = static_cast<DWORD>(blob.size());
    credential.CredentialBlob = reinterpret_cast<LPBYTE>(blob.data());
    credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
    credential.UserName = user.data();
    return CredWriteW(&credential, 0) == TRUE;
}

std::optional<QString> SecretStore::read(const QString &key) const
{
    const std::wstring target = targetName(key);
    PCREDENTIALW credential = nullptr;
    if (CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &credential) != TRUE) {
        return std::nullopt;
    }
    const QString value =
        QString::fromUtf8(reinterpret_cast<const char *>(credential->CredentialBlob),
                          static_cast<qsizetype>(credential->CredentialBlobSize));
    CredFree(credential);
    return value;
}

bool SecretStore::remove(const QString &key)
{
    const std::wstring target = targetName(key);
    return CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0) == TRUE;
}

QString SecretStore::backendName()
{
    return QStringLiteral("Windows Credential Manager");
}

bool SecretStore::isSecureBackend()
{
    return true;
}
