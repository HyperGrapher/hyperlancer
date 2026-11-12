#pragma once

#include <QObject>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

class AppPaths;
class Integrations;
class Database;
class GmailService;
class JobRunner;

// Settings → Health: one row per thing the app depends on, each with a state ("ok", "warn",
// "error", "checking") and a hint on how to fix it.
class HealthModel : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("HealthModel is created by the application.")

    Q_PROPERTY(QVariantList checks READ checks NOTIFY checksChanged)
    Q_PROPERTY(int problemCount READ problemCount NOTIFY checksChanged)

public:
    HealthModel(const AppPaths &paths, Database &database, JobRunner &runner,
                Integrations &integrations, GmailService &gmail, QObject *parent = nullptr);

    [[nodiscard]] QVariantList checks() const { return m_checks; }
    [[nodiscard]] int problemCount() const;

    Q_INVOKABLE void refresh();

signals:
    void checksChanged();

private:
    void setCheck(const QString &id, const QString &title, const QString &state,
                  const QString &detail, const QString &hint = {});
    void checkPythonTools();

    const AppPaths &m_paths;
    Database &m_database;
    JobRunner &m_runner;
    Integrations &m_integrations;
    GmailService &m_gmail;
    QVariantList m_checks;
};
