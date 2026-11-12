# Progress

Read this first when you continue in a new session. It says where the Qt app stands, what was
decided and why, and what to do next. The specification is
[PRD-Freelance-lead-generation-desktop-app.md](PRD-Freelance-lead-generation-desktop-app.md).

- **Branches:** work on `dev`; `master` is fast-forwarded to `dev` for releases
- **Last updated:** 2026-11-12 (session 1)

## 1. Decisions confirmed by the user

| Topic | Decision |
| --- | --- |
| Target platform | Windows 10 PC, MinGW kit. Linux is for development and CI only. |
| Qt version | **Qt 6.11.x** (CI and this environment: 6.11.3). The template's 6.4/6.8 support is dropped. |
| Database | **Local SQLite by default**, and the user can enter **remote MySQL** credentials in Settings → Database. Both backends are supported from the start (see §4). |
| Gmail | Google **Workspace** account, consent screen **Internal**. The user imports the `client_secret_*.json` in the app; ID and secret go to Windows Credential Manager. |
| Google Places | **Not available** yet. Discovery uses LangSearch web search instead. |
| LangSearch | Available. Key must be rotatable in the app (Settings → Connections → Replace). |
| PageSpeed Insights | Enabled in the same Google Cloud project as Gmail. Needs an **API key** (starts with `AIza`); the OAuth client secret is refused when saving. Without a key, anonymous calls on Google's small shared quota. (The Gmail OAuth token is no longer used for PageSpeed since v0.2.2: it only carries the Gmail scope.) |
| QWebSocket | Linked now (`Qt6::WebSockets`) for a later feature; not used yet. |
| Design | Keep the template's look and motion; no copy of the web dashboard. Sites list = standard table rows. Static "idea" chips were removed from Discover at the user's request. |

## 2. What is built (session 1)

### Foundation
- App renamed Lumina → **Hyperlancer** (target, QML module `Hyperlancer`, org `HyperGrapher`, data folder `%APPDATA%\HyperGrapher\Hyperlancer`). Mock template code (TaskSimulator, CollectionModel, Overview/Collections/Activity pages) removed.
- `HyperlancerCore` static library (everything except QML-facing classes) so tests link the real code.
- `Database`: one connection on its own thread; `run(context, work, done)` / `post` / `runBlocking`. No query runs on the UI thread. MySQL connections are pinged after 2 min idle and reconnected. `testConnection` uses a separate short-lived connection.
- `Migrations`: numbered SQL in `resources/migrations/sqlite` and `resources/migrations/mysql` (same versions; a test enforces that). Applied at start; failure shows a banner on every page.
- `SecretStore`: Windows Credential Manager (`SecretStoreWin.cpp`, entries `Hyperlancer/<key>`); dev-only INI fallback on Linux (`secrets-dev.ini` in the data folder).
- `LogFile`: daily log in `<data>/logs`, 14 days kept.
- `UrlNormalizer` + `PublicSuffixList` (bundled PSL, ICANN section) ported from PHP.

### Analysis pipeline
- Python: `site_profile/summary.py` (port of PHP `ProfileNormalizer`, PRD decision 2). `profile.json` now has a `summary` key with a `facts` object (columns copied onto `sites`). Tests: `scripts/tests/test_summary.py`. All 88 pytest tests pass.
- `JobRunner`: QProcess per job (no shell), limits (profile 3), 120 s timeout, cancel file then kill after 5 s, NDJSON progress, heartbeat every 2 s, interrupted jobs failed at start-up, close-guard dialog when jobs run.
- `ProfileIngest`: one transaction: snapshot (raw without summary + summary), technologies, site facts, activity. Failed attempts are stored and keep the last good profile visible.
- `PageSpeedClient` (C++): mobile score + metrics → `speed_checks`, `sites.mobile_score`.

### UI (sections and moods)
| Section | Mood | Content |
| --- | --- | --- |
| Dashboard | Nebula | Greeting, "Analyze a website" hero with the energy orb, 4 stat tiles, Recently analyzed, Expiring soon, API usage |
| Sites | Lagoon | Search, filter (All / Not analyzed / Unreachable / Expiring), table rows with live progress; detail view slides over the list |
| Site detail | Lagoon | Header + actions, 8-step progress track, facts strip (HTTP, CMS, Hosting, SSL, Domain, Mobile score ring), tabs Profile / Emails / History / Activity |
| Discover | Ember | LangSearch query, count, freshness; results grouped per website (New / Known / Added / Same site), select, Add or Add and analyze; recent searches reopen stored results |
| Jobs | Glacier | Status card + table with progress, cancel, retry, open log |
| Settings | Moss | Tabs: Connections (LangSearch, PageSpeed, Gmail), Database (SQLite/MySQL, test, save and restart), Tools (Python paths), Appearance, Health |

Sidebar bottom card shows live background jobs (and a database warning). New controls in `src/qml/controls/`: Badge, EmptyState, FactRow, IconButton, SectionPanel, SiteBadge, SiteTableRow, StatTile, StatusDot, StyledTextField, TableHeader, ThinProgress, Toast. `Format.qml` holds formatting helpers.

### Verified in this environment (Linux, Qt 6.11.3, Xvfb)
- `ctest`: 7 suites pass. Migration test also passes against MariaDB 10.11 (`HYPERLANCER_TEST_MYSQL`).
- End to end with SQLite **and** with MariaDB: add site → job runs Python profiler → live steps → snapshot rendered; Discover with the real LangSearch key → import + analyze 8 sites, 3 at a time.
- Tour (`--tour`): all sections, rapid switching check prints `TOUR stress OK`.
- `SecretStoreWin.cpp` compiles with MinGW (syntax check only; not run).

### Polish round (after session 1)
- Website favicons in badges (`sites.favicon_url`; Settings → Appearance → "Website favicons" turns them off; the letter is the fallback).
- "Sites" renamed "Websites" in the sidebar and page title; the sidebar "Background jobs" card was removed (the Jobs section stays).
- Site detail header buttons share one height (40 px).
- Emails tab: click a row to read the body (fetched from Gmail on demand, never stored); icon button opens the thread in Gmail, in the Chrome profile named in Settings → Connections (`ChromeProfile`, reads Chrome's `Local State`; falls back to the default browser).
- Not built here (Qt 6.11 missing in this environment): only `GmailQuery::bodyText` was compiled and tested.

### Organize round (migration 0002)
- Per site: lead **status** (new, contacted, replied, client, rejected), one **category**, **tags**, **notes**, **to-dos** (optional due date). Tables `categories`, `tags`, `site_tags`, `notes`, `todos`, columns `sites.status` and `sites.category_id`. Code: `SiteOrganizer` (store functions) and `SiteDetail` (invokables; every change reloads the detail and refreshes the list). New detail tab **Workspace** (`SiteWorkspace.qml`).
- Websites list: Status column, category/tags/open to-dos in the second line, status filter row, click a column header to sort (Site, Status, CMS, HTTP, Analyzed; empty values always last). Search also matches tags and categories.
- Tested on SQLite with Qt 6.4 (`tst_siteorganizer`: 7 pass). Not run: MySQL migration 0002, the QML (Qt 6.11 missing here).
- Still open from §5: saved views, snapshot diff, bulk actions, a global to-do list.

### Workflow round (migration 0003)
- **Statuses** are now none, consider, design, contacted, replied, client, completed, canceled (the old "new" became "none"). Edited in the Websites list through a dropdown and in the Workspace tab.
- **Websites list:** columns Site, Status, Category, Tags, Deadline, To-dos, Analyzed (sortable). Filters are dropdowns: status, category, health (all / unreachable / expiring). "Not analyzed", HTTP, SSL, Domain, Hosting and CMS are gone from the list.
- **Workspace tab:** status, one category (dropdown with search and "create"), deadline (calendar picker), tags (dropdown with search and "create"), to-dos with a calendar date, notes with a title (accordion, Markdown rendered by Qt's own `Text.MarkdownText`, so no library was added).
- **Emails:** new section with the inbox (messages in INBOX, not archived). Clicking a message opens `MailView` (plain text; the body is fetched on open and never stored). The site's Emails tab uses the same view.
- **Mail automation:** a sent message to a site's domain moves the status to contacted, a real reply (not an auto-reply or bounce) to replied. Mail never moves a status backwards and never touches client, completed or canceled (`SiteOrganizer::applyMailStatus`).
- **Background mail check:** `GmailInbox` polls `users.history.list` from the stored `historyId` every 60 s (falls back to the current position when Gmail answers 404). New inbox mail shows a desktop notification (Settings → Connections → "Notify about new mail"); mail from a tracked domain is also stored and can move the status.
- **Tray:** `TrayIcon` (Qt Widgets; the app now runs on `QApplication`). Closing the window hides it; the tray menu has Show and Quit (Quit asks first when analyses run). Without a system tray the window closes normally. There is no icon file yet: the icon is drawn in code.
- Verified on Linux with Qt 6.11.3 (installed here with `aqt install-qt linux desktop 6.11.3 linux_gcc_64 -m qtshadertools qtwebsockets qtnetworkauth qttasktree -O /opt/Qt`, plus the xcb libraries from ci.yml): full build, `ctest` 9 of 9, and the tour. The tour now also covers Emails, a sample website with its Workspace tab, the calendar popup, the tag dropdown and a Markdown note (`08`–`11` screenshots). Not verified: the tray and notifications (no tray under Xvfb), a real Gmail inbox, MySQL migration 0003, Windows.

### Speed and screenshots (migration 0004)
- The site's speed button now runs PageSpeed Insights twice (mobile and desktop, two API units). Both scores show as rings in the facts strip; the Performance section lists both. Mobile still feeds `sites.mobile_score`.
- Each call returns Lighthouse's `final-screenshot` (a JPEG). It is saved in `<data>/screenshots/<site id>/<kind>-<ms>.jpg` and recorded in the new `screenshots` table (relative path). Only the newest of each kind is kept; deleting a website removes its folder. New **Screenshots** tab on the website detail page.
- Tests: `tst_langsearch` (screenshot parsing), `tst_siteorganizer` (storage, per-strategy scores). Tour step `09e`.
- The full-page screenshot audit exists in the same response but was left out (large); a later Playwright `site_render` job (PRD M3) will give real 1440×900 and 390×844 captures.

### CI and releases
- Windows: aqtinstall 3.3 cannot read Qt 6.11's new Windows package layout (`qt6_6113/qt6_6113_mingw/`). `.github/scripts/install_qt_windows.py` installs the MinGW kit from the official `Updates.xml` (SHA-1 checked); the install action only provides the MinGW 13.1 toolchain. Linux still uses the action.
- The official Windows kit has **no `qsqlmysql.dll`**. `.github/scripts/build_mysql_driver.sh` builds it from the qtbase sources against MSYS2's `libmariadbclient` and installs it into the kit; the release zip includes it plus `libmariadb.dll` and its DLL dependencies.
- The Windows release zip also contains `web-dashboard/scripts` (run `uv sync` in it once).
- Releases: push a `v*.*.*` tag, or run "Build portable releases" manually with a version input; the manual run creates the tag together with the GitHub Release. (Cloud agent sessions may not be allowed to push tags; they use the manual run through the GitHub API.)
- The Linux AppImage keeps only the SQLite and MySQL drivers; the other Qt SQL plugins need client libraries that are not installed.
- `v0.2.0` was released from `master` at the end of session 1.

**Not verified here:** anything on Windows; Gmail sign-in (no client file in this environment); a successful PageSpeed call (anonymous quota was exhausted, the 429 was handled and shown); real SSL dates (this environment's proxy re-signs TLS, so every SSL shows ~30 days and issuer "Anthropic").

## 3. What the user should check on Windows

1. Install Qt 6.11 components: Shader Tools, WebSockets, Network Authorization (Maintenance Tool). Build as in README.
2. `uv sync --project web-dashboard\scripts`, start the app, Settings → Health should be green except Gmail/PageSpeed until set up.
3. Analyze one site from the Dashboard; watch the steps; open the Profile tab.
4. Settings → Connections: add the LangSearch key, Test; Discover a niche + place; Add and analyze.
5. Gmail: Import file… → Connect → approve in browser → open a site that you have emailed → Emails tab.
6. PageSpeed: add an API key (recommended) → site → Mobile speed.
7. Optional: Settings → Database → MySQL → Test connection → Save and restart. If "MySQL driver" is red in Health, see README "MySQL driver on Windows".
8. Check the credential entries in Windows Credential Manager (`Hyperlancer/...`).

## 4. Design notes for the next agent

- **Two SQL dialects.** Keep store SQL portable: bind times as strings via `DbTime::toDb` (UTC `yyyy-MM-dd HH:mm:ss.zzz`), read with `DbTime::fromDb`; use `SqlExec::insertIgnore(db)`; avoid `ON CONFLICT`/`ON DUPLICATE KEY` (update-then-insert instead); avoid reserved words (`key` → table `app_settings.name`). Every schema change needs a migration in **both** folders with the same number.
- MySQL servers close idle connections after `wait_timeout` (hosted servers: often 30–60 s). `Database::connection()` pings with `SELECT 1` before work when the connection was idle more than 5 s and reconnects if needed (fixed in v0.2.1 after "Server has gone away" on Windows). `tst_database` reproduces it with `SET SESSION wait_timeout = 2`.
- `Database::run` skips the work and returns a default-constructed result when the connection is not open; result types must be default-constructible and carry their own error text.
- `Integrations` was first called `Connections`, which clashed with QtQuick's `Connections` type in QML. Avoid QML type names for C++ classes.
- `Database` lives in the core library without Qt Qml; QML sees it through `DatabaseForeign` (QML_FOREIGN) in `AppContext.h`.
- `businesses` table is **not** created yet: sites are the lead record for now; `mail_messages.site_id` and `discovery_results.site_id` stand in for `business_id`. Introduce `businesses` with M4/M5 and migrate these links.
- LangSearch: quoted exact phrases return 0 results; results contain many throwaway blog subdomains (filtered by `looksLikeBlogFarm` and `ignoredDomains` in `Discovery.cpp`); titles may contain escaped markup (cleaned by `LangSearch::cleanText`).
- PSL rules with non-ASCII labels are stored both as written and as punycode.

## 5. Next steps (suggested order)

1. User's Windows check (§3); fix what comes back.
2. M1 parity gaps: snapshot diff view (port `SnapshotDiff.php`), notes, to-dos, tags/categories, saved views; content crawl (`site_crawl`) as a job type.
3. M2 new checks in Python (schema v3): email authentication (SPF/DMARC/DKIM verdicts), static + GTM tracker detection (`trackers.yaml`), extended contact channels (WhatsApp, phonenumbers). Show them as new profile sections.
4. M3 `site_render` (Playwright screenshots, rendered tracker pass); show screenshots on the detail page.
5. M5 scoring engine (C++), tiers, observations; `businesses` table and pipeline views.
6. M6 outreach: templates, composer, follow-ups, do-not-contact; Gmail background sync with `historyId`, sent/reply/bounce detection.
7. Quota guard: monthly budgets for LangSearch/PageSpeed in settings (usage counters exist in `api_usage`).

## 6. Security note

`docs/LangSearch-apikey.txt` holds a live API key and is committed to the repository. Rotate the
key in the LangSearch dashboard, paste the new one in the app, and remove the file from the
repository (and its history if the repository is or becomes public).
