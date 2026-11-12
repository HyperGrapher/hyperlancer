#pragma once

#include <vector>

#include <QAbstractListModel>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

class Integrations;
class Database;
class JobRunner;
class LangSearchClient;

// Lead discovery through the LangSearch web search API (PRD "Web search API" source). Each
// search is recorded as a discovery run; results are shown one row per website, marked as
// new or already known (deduplicated on registrable domain), and selected rows are imported
// as sites, optionally queued for analysis.
class Discovery : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Discovery is created by the application.")

    Q_PROPERTY(bool busy READ isBusy NOTIFY busyChanged)
    Q_PROPERTY(QString error READ error NOTIFY resultsChanged)
    Q_PROPERTY(QString lastQuery READ lastQuery NOTIFY resultsChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY resultsChanged)
    Q_PROPERTY(int newCount READ newCount NOTIFY resultsChanged)
    Q_PROPERTY(int selectedCount READ selectedCount NOTIFY selectionChanged)
    Q_PROPERTY(QVariantList recentRuns READ recentRuns NOTIFY recentRunsChanged)

public:
    enum Role
    {
        UrlRole = Qt::UserRole + 1,
        DomainRole,
        TitleRole,
        SnippetRole,
        KnownRole,
        SelectedRole,
        ImportedRole,
        DuplicateRole
    };

    Discovery(Database &database, JobRunner &runner, LangSearchClient &client,
              Integrations &integrations, QObject *parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] bool isBusy() const { return m_busy; }
    [[nodiscard]] QString error() const { return m_error; }
    [[nodiscard]] QString lastQuery() const { return m_lastQuery; }
    [[nodiscard]] int newCount() const;
    [[nodiscard]] int selectedCount() const;
    [[nodiscard]] QVariantList recentRuns() const { return m_recentRuns; }

    // freshness: noLimit, oneYear, oneMonth, oneWeek, oneDay.
    Q_INVOKABLE void search(const QString &query, int count, const QString &freshness);
    Q_INVOKABLE void toggle(int row);
    Q_INVOKABLE void selectAllNew();
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE void importSelected(bool analyze);
    Q_INVOKABLE void loadRecentRuns();
    // Shows the stored results of an earlier run without calling the API.
    Q_INVOKABLE void openRun(qint64 runId);

signals:
    void busyChanged();
    // A search ended: ok is false when it failed.
    void searchFinished(bool ok);
    void resultsChanged();
    void selectionChanged();
    void recentRunsChanged();
    void notice(const QString &message, bool isError);

public:
    struct Row
    {
        QString url;    // site root
        QString domain; // registrable domain (or site key)
        QString siteKey;
        QString title;
        QString snippet;
        bool known = false;
        bool selected = false;
        bool imported = false;
        bool duplicate = false; // another row in this run has the same domain
    };

private:
    void setBusy(bool busy);
    void showRows(std::vector<Row> rows, const QString &query, const QString &error);

    Database &m_database;
    JobRunner &m_runner;
    LangSearchClient &m_client;
    Integrations &m_integrations;
    std::vector<Row> m_rows;
    bool m_busy = false;
    QString m_error;
    QString m_lastQuery;
    qint64 m_runId = 0;
    QVariantList m_recentRuns;
};
