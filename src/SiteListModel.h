#pragma once

#include <vector>

#include <QAbstractListModel>
#include <QHash>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

#include "SiteStore.h"

class Database;
class JobRunner;

// The Sites table. Search and filter run in SQL on the database thread; results are applied
// row by row where possible so the view keeps its scroll position and hover state.
class SiteListModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("SiteListModel is created by the application.")

    Q_PROPERTY(QString search READ search WRITE setSearch NOTIFY searchChanged)
    Q_PROPERTY(int filter READ filter WRITE setFilter NOTIFY filterChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(int total READ total NOTIFY countChanged)
    Q_PROPERTY(bool loaded READ isLoaded NOTIFY countChanged)

public:
    enum Role
    {
        SiteIdRole = Qt::UserRole + 1,
        SiteKeyRole,
        UrlRole,
        TitleRole,
        CmsRole,
        HostingRole,
        EmailProviderRole,
        HttpStatusRole,
        OutcomeRole,
        LastFetchedAtRole,
        TlsValidUntilRole,
        DomainExpiresOnRole,
        SourceRole,
        FaviconUrlRole,
        JobStatusRole,
        ProgressRole,
        ProgressLabelRole
    };

    SiteListModel(Database &database, JobRunner &runner, QObject *parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] QString search() const { return m_search; }
    void setSearch(const QString &search);
    [[nodiscard]] int filter() const { return static_cast<int>(m_filter); }
    void setFilter(int filter);
    [[nodiscard]] int total() const { return m_total; }
    [[nodiscard]] bool isLoaded() const { return m_loaded; }

    Q_INVOKABLE void refresh();
    // Debounced refresh, for bursts of changes.
    Q_INVOKABLE void refreshSoon();

signals:
    void searchChanged();
    void filterChanged();
    void countChanged();

private:
    struct Progress
    {
        double fraction = 0.0;
        QString label;
    };

    void apply(SiteStore::ListResult result);
    [[nodiscard]] int rowOf(qint64 siteId) const;

    Database &m_database;
    std::vector<SiteStore::SiteRow> m_rows;
    QHash<qint64, Progress> m_progress; // live progress of running analyses, by site
    QString m_search;
    SiteStore::Filter m_filter = SiteStore::Filter::All;
    int m_total = 0;
    bool m_loaded = false;
    int m_generation = 0; // drops results of superseded queries
    QTimer m_refreshDelay;
};
