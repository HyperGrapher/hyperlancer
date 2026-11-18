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

### Dashboard: coming up
- New "Open to-dos" tile (with the overdue count) and a "Coming up" panel: open to-dos and website deadlines that are overdue or due within 30 days, soonest first (up to 8); a click opens the website. Query in `SiteStore::dashboard`, test `dashboardListsDueTodosAndDeadlines`.
- Still open: a global to-do list page, websites per status on the dashboard.

### Fixes after use (mail notices, notes, dropdowns)
- **New-mail notices** are now an always-on-top window in the screen corner (`MailNotifications.qml`, opened from `Main.qml`), not a tray balloon: a notice stays until the user clicks Dismiss, Open (shows the Emails section) or Dismiss all, or opens the Emails page. The tray icon no longer shows balloons.
- **Emails badge:** the sidebar entry shows how many messages arrived since the Emails page was last open (`GmailInbox.newCount`, cleared by `markSeen()`).
- **Notes:** opening a note scrolls it to the top of the Workspace viewport (extra room is added below while one is open).
- **Click-through:** a click on a dropdown or calendar popup also hit the row behind it, because TapHandler is passive. Popups now have a MouseArea behind their content and options use MouseArea. Reproduced and verified with real mouse clicks under Xvfb (`xdotool`): the old code opened the website, the new code does not. Rule: items inside a Popup that sits over other clickable items must use MouseArea, not TapHandler.
- Known cosmetic: Markdown links in notes use Qt's default blue (the importer ignores `Text.linkColor`).

### Sounds
- `resources/sounds/*.mp3` are bundled automatically (CMake globs the folder) and offered in Settings → Appearance → **Sounds**: on/off, volume, and one sound per event with a play button ("No sound" silences one event). Events live in `SoundEvents.h` (new email, analysis finished, analysis failed, speed check finished, discovery finished, other errors) with defaults (message, ding, error, ding, idea, error). Assignments are stored in `AppSettings` (`sounds/event/<id>`; a stored empty string means no sound).
- `SoundPlayer` (Qt Multimedia, one `QMediaPlayer` per file) is wired in `AppContext` to the inbox, job runner, speed check, discovery and every error notice; events within 400 ms of each other play once. Adding a new sound = drop an `.mp3` in the folder; adding a new event = one line in `SoundEvents.h` and a connection in `AppContext`.
- New dependency: **Qt Multimedia** (README, `ci.yml`, `release.yml` and the Windows install cache key updated). MP3 decoding works with Qt's bundled FFmpeg (checked on Linux); no sound device in the test container, so playback itself was not heard. `contract`, `project`, `conflict` and `click` are available to assign but unused by default.
- Website detail tabs reset their scroll position (and the opened note) when the site changes or the detail view closes.

### Snapshot diff
- History tab: **Changes** on each successful analysis compares it with the newest earlier analysis that did not fail. `SnapshotDiff` (C++ port of `SnapshotDiff.php`, same sections and labels) works on the normalized summaries; `SiteStore::diffWithPrevious` finds the pair; `SiteDetail::compareSnapshot` / `snapshotDiff` expose it; `SnapshotChanges.qml` shows added / removed / changed rows per section and lists the unchanged sections.
- Tests: `tst_snapshotdiff` (4) and `diffComparesWithTheEarlierSuccessfulAnalysis` in `tst_siteorganizer`. Checked visually with two seeded analyses.
- Still open from §5: saved views, bulk actions, global to-do list, content crawl, M2 checks.

### Bulk add and bulk actions
- **Add many websites** (button next to Analyze on the Websites page): paste or type addresses separated by new lines, spaces, commas or semicolons; duplicates inside the text are merged; "Analyze them after adding" queues the new ones (three at a time). The result line says how many were added, were already tracked, or were not valid addresses; only the invalid entries stay in the box. Code: `AppContext::addSites` (one transaction).
- **Selection and bulk actions:** a checkbox on each row and a select-all box in the header. While rows are selected a bar replaces the filters: set status, set category (search or create), add tag (search or create), analyze, remove (with confirmation), clear. Code: `SiteListModel` selection (`toggleSelected`, `selectAll`, `setStatusForSelected`, ...) and `AppContext::removeSites`. A filter that hides a row also drops it from the selection.
- Discovery/search is parked on purpose (results are mostly noise); the Discover page still exists.
- Still open: saved views, global to-do list, content crawl, M2 checks.

### Saved views and the To-dos page (migration 0005)
- **Saved views** (Websites toolbar): a dropdown of named views. Pick one to apply its search, status, category, health filter and sort; type a name and Enter to save the current ones (an existing name is replaced); the trash button (double-click) deletes the active view. The view stays "active" until search, filters or sort change. Table `saved_views`; code in `SiteListModel` (`saveView`, `applyView`, `deleteView`) and `SiteStore`.
- **To-dos page** (new sidebar section between Emails and Jobs; Jobs and Settings moved to 5 and 6): all open to-dos of all websites grouped Overdue / Today / Next 7 days / Later / No date, or the completed ones; tick off, delete, click the website to open it. Code: `TodoList`, `TodosPage.qml`.
- The Websites toolbar columns were re-sized so the saved-views dropdown fits.

### M2 checks: email authentication and trackers (summary version 2)
- Python (`site_profile/mailauth.py`): SPF (qualifier, include chain with the DNS lookup count against the limit of 10, multiple-record error), DMARC (policy, sp, pct, rua, ruf, adkim, aspf) and DKIM probing (provider selectors first, then a common list; "no selector answered" is *unknown*, not missing). One verdict per record (pass / weak / missing) and an overall label. The probing list is a constant for now (not yet editable in Settings).
- Python (`site_profile/trackers.py` + `site_profile/data/trackers.json`): static detection from the page HTML of Google Ads, GA4, Universal Analytics (deprecated), Tag Manager, Meta Pixel, TikTok, LinkedIn, Microsoft UET, Yandex, X, Pinterest, Snapchat, Criteo, Hotjar and Clarity with IDs, plus `runsAds`, `adPlatforms`, `staleAnalytics`, `hasTagManager`. Rules are JSON instead of the PRD's YAML to avoid a new dependency. The rendered pass (tags injected by Tag Manager) needs the later `site_render` job.
- The summary now has `emailAuth` and `trackers` (SUMMARY_VERSION 2); the Profile tab shows "Email authentication" and "Ads and analytics". 11 new pytest tests (99 pass).
- Not done from the PRD's contact-channels item: phone normalization (needs the `phonenumbers` dependency), contact-page crawling and person extraction.

### Content crawl, waiting status, version tag (migration 0006)
- **Content crawl** (website detail → new **Content** tab): runs the existing `site_crawl` tool as a job (type `crawl`, one at a time, 2 hour limit, cancel, live progress). Saves every page as Markdown plus images and documents under `jobs/<job id>/crawl` in the data folder; only the latest crawl of a website is kept (older folders are deleted; deleting the website removes them). The tab shows the summary, whether it stopped at the page limit, the page list (open the page or its Markdown file) and "Open folder". Up to 50/100/200/500 pages per crawl. Code: `JobRunner::enqueueCrawl` (new `crawlProgress`/`crawlFinished` signals so a crawl is never mistaken for an analysis), `CrawlStore`, `SiteDetail::startCrawl`, `SiteContent.qml`, table `crawls`. A new sound event "Content crawl finished".
- **Browser capture** (website detail → **Screenshots** tab → "Capture in browser"): new Python tool `site_render` (Playwright, headless). It uses the Chrome or Edge already installed on the computer (path can be given as `browserPath`; no browser is downloaded). It saves a desktop (1440×900), a mobile (390×844, touch) and a full-page screenshot, waits a few seconds for late scripts, and records every request the page made. The same tracker rules as the HTML pass run on those requests (`trackers.detect_requests`, source "rendered"), so tags that Tag Manager loads at run time show in "Ads and analytics" with "loaded at run time". Every request goes through the same network guard as the other tools. Job type `render` (one at a time, 3 minute limit). Code: `JobRunner::enqueueRender`, `RenderStore` (table `renders`, migration 0007; only the newest render is kept), `SiteDetail::startRender`, `SiteScreenshots.qml` (full page scrolls). Screenshots now carry a source (`pagespeed` or `render`); a render replaces the earlier picture of the same kind. Not tested on Windows or with Edge. Python tests: `tests/test_render.py`.
- Verified end to end against a local test site (3 pages, 1 image) with real clicks; the crawl needs `allowPrivate` for local addresses, which the app does not offer, so that was a temporary test-only change (reverted). Not tested: a big public site, Windows.
- New status **Waiting** (between Contacted and Replied; mail never moves a status backwards, so a sent mail leaves Waiting alone).
- The sidebar shows the version at the bottom; the release workflow passes the tag as `HYPERLANCER_VERSION`, other builds show the project version (0.2).
- The Fingerprint line is gone from SSL / TLS.

### Wix sites in the content crawler (`site_crawl/wix.py`)
Found on a real Wix site (en.cankayaenerji.com.tr):
- **Previews instead of originals:** Wix markup only references resize URLs (`.../media/<file>/v1/fill/w_514,...`). The crawler now tries the uploaded file (`.../media/<file>`) first and keeps the preview as the fallback (`wix.original_url`; applies to any `static.wixstatic.com` image, Wix site or not).
- **Gallery images missing:** Pro Gallery marks items outside the viewport `aria-hidden="true"`, which the renderer treated as hidden. On Wix pages `aria-hidden` no longer hides anything. The markup only holds the first ~25 items; the rest were found by watching the page in a headless browser: it pages `GET /pro-gallery-webapp/v1/galleries/<id>?offset=N&limit=...` with the site's public access token (`/_api/v1/access-tokens`, app `22bef345-...`) as `Authorization`. `wix.fetch_all_items` does the same (100 per request, up to 2000 items; videos become their largest poster frame). 220 of 220 items on the test site. Galleries are scoped to components inside the page content (not header/footer).
- **Missing text:** `tidy()` dropped any heading followed by another heading of the same level, which removed title-only headings such as a feature grid. It now only drops a heading followed by a *higher*-level heading or by nothing (this change applies to every site).
- Not covered: Wix galleries other than Pro Gallery, CSS background images, Wix Stores/Blog item lists, and the planned headless-browser render pass. 12 new pytest tests (`tests/test_crawl_wix.py`), 106 pass; the WordPress crawl tests are unchanged and pass.
- `Fetcher.get` takes optional request headers now.

### Contact channels (summary `contact.channels`)
- Python `site_profile/channels.py` (new dependency `phonenumbers`): scans the homepage and up to five contact-like pages found from its links (iletisim, bize-ulasin, contact, kontakt, hakkimizda, about, ...). Phones normalised to E.164 with line type (mobile / landline / ...), default region from the domain suffix; WhatsApp numbers from chat links (`wa.me`, `api.whatsapp.com/send`, `whatsapp://send`) are *confirmed*, a mobile number without a link is only *likely* (never checked against WhatsApp); chat widgets (Joinchat, Click to Chat, GetButton, Elfsight); emails labelled generic or personal with the pages they were found on; people from schema.org Person.
- The Profile tab has a "Reach out" section with a button to open a WhatsApp chat (`https://wa.me/<number>`) or write an email. Checked on en.cankayaenerji.com.tr (found the number on three pages, labelled likely). 7 new pytest tests (113 pass).
- Still open: people from team pages without schema.org markup, a message template for the WhatsApp chat, the outreach composer (M6).

### CI and releases
- Windows: aqtinstall 3.3 cannot read Qt 6.11's new Windows package layout (`qt6_6113/qt6_6113_mingw/`). `.github/scripts/install_qt_windows.py` installs the MinGW kit from the official `Updates.xml` (SHA-1 checked); the install action only provides the MinGW 13.1 toolchain. Linux still uses the action.
- The official Windows kit has **no `qsqlmysql.dll`**. `.github/scripts/build_mysql_driver.sh` builds it from the qtbase sources against MSYS2's `libmariadbclient` and installs it into the kit; the release zip includes it plus `libmariadb.dll` and its DLL dependencies.
- The Windows release zip also contains `tools` (run `uv sync` in it once).
- Releases: push a `v*.*.*` tag, or run "Build portable releases" manually with a version input; the manual run creates the tag together with the GitHub Release. (Cloud agent sessions may not be allowed to push tags; they use the manual run through the GitHub API.)
- The Linux AppImage keeps only the SQLite and MySQL drivers; the other Qt SQL plugins need client libraries that are not installed.
- `v0.2.0` was released from `master` at the end of session 1.

**Not verified here:** anything on Windows; Gmail sign-in (no client file in this environment); a successful PageSpeed call (anonymous quota was exhausted, the 429 was handled and shown); real SSL dates (this environment's proxy re-signs TLS, so every SSL shows ~30 days and issuer "Anthropic").

## 3. What the user should check on Windows

1. Install Qt 6.11 components: Shader Tools, WebSockets, Network Authorization, Multimedia (Maintenance Tool). Build as in README.
2. `uv sync --project tools`, start the app, Settings → Health should be green except Gmail/PageSpeed until set up.
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
- **DNS fallback:** if the computer's own DNS resolver fails, times out, or answers "name does not exist" (seen on a Windows PC whose first DNS server, 127.0.0.1, said NXDOMAIN for example.com), the profile retries with public resolvers (1.1.1.1, 8.8.8.8, 9.9.9.9); a resolver chosen by the user is never replaced. Fixes empty Hosting, DNS, Email and Email authentication on networks where the system resolver does not answer. Test: `tests/test_dns_fallback.py`.
- **To-dos All tab, Notes page, editable notes:** To-dos has a third tab **All**: every to-do (done or not, with or without a date) under its website's domain; click the domain to open the website. New sidebar section **Notes** (below To-dos, Ctrl+6; Jobs and Settings moved to 7 and 8): every note of every website under its domain, click to read the Markdown; read-only there. Notes are edited on the website's Workspace tab (Edit button on an opened note: title, body, Save/Cancel; `SiteOrganizer::updateNote`). Code: `TodoList::filter` (replaces `showDone`), `NoteList`, `NotesPage.qml`. Fixed `AppSettings::kSectionCount` (was 5, so the last page was not restored for Jobs and Settings). Tags that Tag Manager loads: Screenshots tab → Capture in browser, then Profile tab → Ads and analytics.
- **v0.3.0 round:** (1) The Python tools moved from `web-dashboard/scripts` to `tools/` at the project root and are part of the app now; `web-dashboard/` stays only as a reference. `ToolPaths` looks for `tools/pyproject.toml` upward from the executable; the release zip has a `tools` folder (run `uv sync --project tools` once). A tools folder saved in Settings that still points to the old path must be changed. (2) Long notes scroll inside their text box (add and edit). (3) Clicking a screenshot opens it in the default picture app. (4) Content tab: click a page to see its downloaded images as a grid (from `assets.json`, `CrawlStore::images`); click an image to open it in the default app; each page row shows its image count. (5) Web archive capture dates have an open-in-browser button. (6) Sidebar order is now Dashboard, Websites, Emails, To-dos, Notes, Jobs, Discover, Settings (Ctrl+1 to Ctrl+8; the saved "last page" number changed meaning, so the first start may open another page).
- **ASP.NET sites (v0.3.1):** the content crawler dropped every page of an ASP.NET WebForms site, because the whole page sits inside one `<form>` and forms were skipped as noise. A form that contains the page parts (header, footer, main, article, h1, h2) is now read as the page; small forms (search, login) are still skipped. Lazy-loaded images use `data-src` before a blank placeholder `src` (sliders). The header and footer text (without menus) is saved once per crawl in `site.md` and left out of the pages, so contact details in the footer are not lost. Tests: `tools/tests/test_crawl_aspnet.py`.
- **Content page filter (v0.3.2):** a search box above the page list on the Content tab; it matches words in the page title or address (all words must match, case does not matter). Cleared when another website opens.
- **Crawler, meta refresh and header/footer images (v0.3.3):** a page that only redirects with `<meta http-equiv=refresh>` (otelseyhan.com.tr/ sends to tr/) is now followed; before, the home page was empty and nothing was downloaded from it. Images in the header and footer (logo, footer gallery) are downloaded once and listed in `site.md` (first page with a header or footer only).
- **Visitors page (v0.4.0):** live visitor data of the portfolio site (burak-react, Convex backend). `ConvexClient` (core library, `QWebSocket` + `QJsonDocument`) is a minimal client of the Convex sync protocol: Connect, ModifyQuerySet (add/remove query), Transition and TransitionChunk (results come as full values, not diffs), reconnect with backoff, 60 s silence watchdog. No sign-in, mutations or actions: the site's own `stats:*` queries take an `adminKey` argument. The protocol is internal to Convex; the client version in the URL is pinned (`1.49.0`, checked against the open-source `convex` package). `VisitorStats` subscribes to `stats:recentSessions` and `stats:recentVisitors` (limit 100, bots included, so the Bots switch is a local filter) and, while a visitor is open, `stats:visitorDetail` and `stats:sessionEvents` (answers `{ events, capped }`). Settings → Connections → Website visitors: Convex address (setting) and access key (secret store `convex/admin-key`, the Convex `ADMIN_KEY`), plus a switch for notices. Sidebar order is now Dashboard, Websites, Emails, To-dos, Notes, Visitors, Jobs, Discover, Settings (Ctrl+1..9; `kSectionCount` 9). Page: Sessions or Visitors list, Bots switch, live dot, click a row for the visitor page (numbers, all sessions, session details, live event timeline). New non-bot session (also a bot that is cleared later) → notice window in the top-right corner (stays until dismissed; Open goes to the visitor) and the sound "New website visitor" (assignable in Appearance). The first data after start-up is silent. Tests: `tst_convexclient` (local WebSocket server), `tst_visitorstats` (skipped on Windows because it would write the real credential manager). Not tried on Windows.
- **Due-date reminders (v0.4.1):** `DueNotifier` checks every 5 minutes and at start-up for website deadlines and open to-dos that are due today or overdue (sites that are completed or canceled, archived sites, done to-dos and undated items are skipped). Each item is announced once per day and stage (today, overdue); the keys are kept in the settings (`notices/dueShown`) so a restart does not repeat a notice, and an overdue item comes back the next day. A notice window (`DueNotifications.qml`, top-right, below the visitor notices, stays until dismissed; Open goes to the website) and the sound "Deadline or to-do due" (assignable in Appearance). Settings → Connections → Reminders switches it off. Test: `tst_duenotifier`.
- **Archive (v0.4.2):** a website can be archived (archive button next to Remove on its page, or select websites in the list and press Archive). Archived sites leave the Websites list, Dashboard, To-dos, Notes and the due-date reminders but keep all their data; the Websites filter "Archived" lists them and the same buttons restore them (`SiteStore::setArchived`, `Filter::Archived`, `AppContext::archiveSite`). The column `sites.archived_at` already existed. Mail matching and discovery still see archived sites as known.
- **Preview link (v0.4.3):** a website has a preview link (the redesign, for example on Vercel): Workspace → Details → Preview link (saved when the field loses focus; a missing `https://` is added; an empty field removes it). When set, an eye button in the website's header and the button beside the field open it in the browser. Uses the existing column `sites.preview_url`; `SiteDetail::setPreviewUrl` checks that it is a web address.
