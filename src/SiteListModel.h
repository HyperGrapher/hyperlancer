#pragma once

#include <functional>
#include <vector>

#include <QAbstractListModel>
#include <QHash>
#include <QSet>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
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
    Q_PROPERTY(QString statusFilter READ statusFilter WRITE setStatusFilter NOTIFY statusFilterChanged)
    Q_PROPERTY(QString categoryFilter READ categoryFilter WRITE setCategoryFilter NOTIFY
                   categoryFilterChanged)
    Q_PROPERTY(QStringList categories READ categories NOTIFY countChanged)
    Q_PROPERTY(QStringList tags READ tags NOTIFY countChanged)
    Q_PROPERTY(int sortKey READ sortKey WRITE setSortKey NOTIFY sortChanged)
    Q_PROPERTY(bool sortDescending READ sortDescending WRITE setSortDescending NOTIFY sortChanged)
    // Saved views: { id, name, config }. activeViewId is the one last applied or saved; it is
    // cleared as soon as search, filters or sort differ from it.
    Q_PROPERTY(QVariantList savedViews READ savedViews NOTIFY savedViewsChanged)
    Q_PROPERTY(qint64 activeViewId READ activeViewId NOTIFY activeViewChanged)
    Q_PROPERTY(int selectedCount READ selectedCount NOTIFY selectionChanged)
    Q_PROPERTY(QVariantList selectedIds READ selectedIds NOTIFY selectionChanged)
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
        SelectedRole,
        StatusRole,
        DeadlineOnRole,
        CategoryRole,
        TagsRole,
        OpenTodosRole,
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
    [[nodiscard]] QString statusFilter() const { return m_statusFilter; }
    void setStatusFilter(const QString &status);
    [[nodiscard]] QString categoryFilter() const { return m_categoryFilter; }
    void setCategoryFilter(const QString &category);
    [[nodiscard]] QStringList categories() const { return m_categories; }
    [[nodiscard]] QStringList tags() const { return m_tags; }
    [[nodiscard]] int sortKey() const { return static_cast<int>(m_sortKey); }
    void setSortKey(int key);
    [[nodiscard]] bool sortDescending() const { return m_sortDescending; }
    void setSortDescending(bool descending);
    [[nodiscard]] int total() const { return m_total; }
    [[nodiscard]] bool isLoaded() const { return m_loaded; }

    // Changes one site's lead status from the list, then refreshes.
    Q_INVOKABLE void setSiteStatus(qint64 siteId, const QString &status);
    // Selection of rows for bulk actions. Only websites in the current list can be selected;
    // a filter or search that hides a row also drops it from the selection.
    [[nodiscard]] int selectedCount() const { return static_cast<int>(m_selected.size()); }
    [[nodiscard]] QVariantList selectedIds() const;
    [[nodiscard]] QVariantList savedViews() const { return m_savedViews; }
    [[nodiscard]] qint64 activeViewId() const { return m_activeViewId; }
    Q_INVOKABLE void loadSavedViews();
    Q_INVOKABLE void saveView(const QString &name);
    Q_INVOKABLE void applyView(qint64 viewId);
    Q_INVOKABLE void deleteView(qint64 viewId);
    Q_INVOKABLE void toggleSelected(qint64 siteId);
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE void setStatusForSelected(const QString &status);
    Q_INVOKABLE void setCategoryForSelected(const QString &name);
    Q_INVOKABLE void addTagToSelected(const QString &name);
    // Queues an analysis for every selected website that is not already being analyzed.
    Q_INVOKABLE void analyzeSelected();
    Q_INVOKABLE void refresh();
    // Debounced refresh, for bursts of changes.
    Q_INVOKABLE void refreshSoon();

signals:
    void searchChanged();
    void filterChanged();
    void statusFilterChanged();
    void categoryFilterChanged();
    void selectionChanged();
    void savedViewsChanged();
    void activeViewChanged();
    void sortChanged();
    void countChanged();

private:
    struct Progress
    {
        double fraction = 0.0;
        QString label;
    };

    void apply(SiteStore::ListResult result);
    [[nodiscard]] int rowOf(qint64 siteId) const;

    [[nodiscard]] QVariantMap currentConfig() const;
    void runOnSelected(std::function<bool(QSqlDatabase &, qint64)> operation);

    Database &m_database;
    JobRunner &m_runner;
    QSet<qint64> m_selected;
    QVariantList m_savedViews;
    qint64 m_activeViewId = 0;
    QVariantMap m_activeViewConfig;
    std::vector<SiteStore::SiteRow> m_rows;
    QHash<qint64, Progress> m_progress; // live progress of running analyses, by site
    QString m_search;
    SiteStore::Filter m_filter = SiteStore::Filter::All;
    QString m_statusFilter;
    QString m_categoryFilter;
    QStringList m_categories;
    QStringList m_tags;
    SiteStore::SortKey m_sortKey = SiteStore::SortKey::Analyzed;
    bool m_sortDescending = true;
    int m_total = 0;
    bool m_loaded = false;
    int m_generation = 0; // drops results of superseded queries
    QTimer m_refreshDelay;
};
