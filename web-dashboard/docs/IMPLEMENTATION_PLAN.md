# Implementation Plan

This plan starts **after the PRD is approved** and the open questions are answered (PRD §13). Every milestone ends with a working build: `pnpm lint && pnpm typecheck && pnpm test && pnpm build` is green, the app runs in the Linux dev container, and there is a short Windows/Appytizer check you can run.

Legend: ☐ task · 🪟 needs verification on your Windows PC · 🧪 has automated tests.

---

## M0: Foundation and Windows spike

**Goal:** a skeleton that builds, serves, migrates and spawns a background process on both OSes. It retires the riskiest unknowns first.

- ☑ Replace the demo `App.tsx`. Add the app shell (sidebar, top bar, routes for Dashboard, Sites, To-dos, Jobs and Settings, each with an empty state). Use TanStack Router with history mode and a hash-mode flag. **Done in the layout preview, with demo data from `src/mocks/` (see `docs/HANDOFF.md`).**
- ☑ Add the shadcn primitives needed (input, select, dialog, dropdown, tabs, badge, card, table, checkbox, popover, tooltip, sonner, skeleton, sheet, progress, collapsible, textarea, label, separator). `cn` stays: it is shadcn's own merge package.
- ☑ Backend skeleton: `bootstrap.php` (autoload, error handler), `Infra/Config` (env → `config.local.php` → OS defaults), `Paths`, `Platform`, `Database` (WAL, FKs, busy timeout), `Logger`.
- ☑ `Http/Router`, `Request`, `Response`, `Guard` (Host allow-list, JSON + custom header on mutations). 🧪
- ☑ `Migrator` + `0001_init.sql` (full MVP schema) + auto-migrate with pre-migration backup + `bin/migrate.php`. 🧪
- ☑ `public/index.php` front controller (`/api/*` → backend, else `index.html`). `backend/dev-router.php` for `php -S`.
- ☑ Vite build plugin:
  - copy `backend/**` minus tests, dev files, data and vendor
  - generate `build-info.php`
  - assert the data dir is outside `dist/`
  - Vite proxy `/api` → `PHP_API_TARGET` (default `http://127.0.0.1:8787`)
- ☑ Legacy guard: detect `backend/data/app.sqlite` or `dist/backend/data`, warn on the Health page, and move with `migrate.php --import-legacy`.
- ☑ `GET /api/health` + Settings → Health page (PHP extensions, SQLite/FTS5, data dir, Python, PHP CLI).
- ☑ **Spawn spike:**
  - `Jobs/Process/{Windows,Posix}Process` + a minimal `worker.php` that runs a `selfcheck` job (sleeps 180 s while heartbeating, so it outlives a php-cgi recycle)
  - a cancel path using `taskkill` or process-group kill
- ☑ Tooling:
  - `package.json` scripts: `dev:api`, `dev:worker`, `test`, `test:php`, `test:py`
  - `backend/composer.json` (dev: phpunit)
  - **one** `scripts/pyproject.toml` + `uv.lock` for both scripts (deps of both + pytest)
  - Vitest config
- ☑ README: setup for Windows and Linux, how it's served by Appytizer, and where the data lives.
- 🪟 **Verify on Windows:**
  - (1) `http://dashboard.test/sites/1` deep link loads the app
  - (2) `/api/health` is all green
  - (3) the selfcheck job survives 3 minutes, can be cancelled, and is visible in Task Manager
  - (4) running `pnpm build` twice leaves `data/` untouched
  - If (3) fails, switch to the Task Scheduler daemon mode and document it.

**State (2026-10-26):** everything above is built and tested on Linux. Only the 🪟 Windows verification is open.

**Exit criteria:** the shell UI is served on both OSes, migrations and backups work, and background spawn plus cancel are proven on Windows (or the daemon fallback is documented).

---

## M1: Profiler v2 (Python)

**Goal:** the profiler is reliable, machine-friendly and richer, with no UI changes yet.

- ☑ Refactor into the `scripts/site_profile/` package, run with `python -m site_profile`. The old `site_profile.py` is deleted: the script is internal to the app, so no CLI compatibility is kept.
- ☑ Shared `scripts/site_tools/`: `events.py` (NDJSON), `exitcodes.py`, `spec.py` (`--spec` JSON), `netguard.py` (SSRF: resolve and block private ranges, re-check on redirect). 🧪
- ☑ Schema v2:
  - `schema_version`, `status`, `errors[]`, `timings_ms`; stable section shapes
  - `schema_v2.json` + a validation test 🧪
- ☑ CLI contract: `--output`, `--events`, a clean `--json`, no hard-coded report dir, exit codes 0/1/2/3/4/130. 🧪
- ☑ Fixes:
  - TLS-failure fallback (unverified re-fetch + `tls.verified=false`)
  - NS retry, fallback and optional resolver
  - TRABIS WHOIS parser 🧪
  - tolerant sitemap parsing (`defusedxml`) with index follow and caps 🧪
  - Wayback 6 s timeout
- ☑ New data:
  - contacts, socials and favicon 🧪
  - TTFB, total time, HTML size, compression; HTTP→HTTPS and www checks
  - ASN/org (Team Cymru DNS lookup instead of IP RDAP: DNS only, no extra HTTP), reverse DNS, email provider from MX
  - WordPress version, theme and plugins 🧪
  - heading outline, hreflang, OG/Twitter, link counts, consent tool, bot-protection detection 🧪
- ☑ Fixture suite: one synthetic agency page (metadata, contacts, WordPress, consent), WHOIS bodies (`.com`, and a real `.tr` TRABIS record (registrant anonymised)), sitemap variants (DOCTYPE, index, gzip, entity bomb) and the 4 existing JSONs as v1 golden files.
- ☐ 🪟 Run `uv run python -m site_profile forazeytin.com.tr --events --output x.json` from `scripts\` on Windows. Check that NS is now filled and that `.tr` shows registrar and expiry.

---

## M2: Job runner

**Goal:** a production-grade queue, worker, progress, logs and cancel, plus the Jobs UI.

- ☑ `JobRepository`: enqueue, claim with a concurrency check (atomic `UPDATE … RETURNING`), progress, heartbeat, finish, cancel flag. 🧪
- ☑ `Worker`:
  - flock lock; runs child processes with array-form `proc_open`
  - non-blocking NDJSON reader (stdout) and stderr → log
  - heartbeat every 2 s, cancel polling every 1 s, soft-then-hard kill
  - per-type timeouts; idle exit after 10 s; `--daemon` mode
  - 🧪 fake-script tests: success, partial, crash, timeout, cancel, orphan
- ☑ `JobLauncher::ensureWorker()` on enqueue and on job/dashboard reads, with orphan recovery (stale heartbeat and a dead worker → `failed`). 🧪
- ☑ Job types: `selfcheck` runs. `profile` (M3), `crawl` (M5), `archive` and `backup` are wired when their features land; until then the worker fails them with "not implemented yet". The command table already supports a per-type working directory.
- ☑ API: `GET /api/jobs`, `/jobs/:id`, `/jobs/:id/log?offset`, `POST /jobs/:id/cancel`, `/jobs/:id/retry`. 🧪
- ☑ UI:
  - Jobs page: active and finished, with filters
  - job drawer: progress, stats, live log tail, cancel and retry
  - top-bar job indicator; toasts on completion
- ☑ Log cap (the worker stops appending at 10 MB) and pruning of job folders older than 30 days (`JobMaintenance`, run when a worker starts). True rotation was dropped: a retry is a new job with its own folder.
- ☐ 🪟 Start 3 selfcheck jobs plus 1 long one; cancel one; reboot mid-job → it shows as failed (orphan) and retry works.

---

## M3: Sites, profiles, list and detail

**Goal:** the core of Feature 1, end to end.

- ☑ `UrlNormalizer` + `PublicSuffixList` (bundled `.dat`, plus a refresh script) + `NetworkGuard` pre-check. 🧪
- ☑ Sites API:
  - `check-url`, create (with optional fetch job), get, patch (activity on changes), archive, restore, delete (typed confirm), bulk
- ☑ `SiteQuery`: allow-listed filters, sort and pagination, plus facet counts. 🧪
- ☑ `ProfileNormalizer` (v1 + v2 → `summary_json`), `ProfileIngestor`, `SnapshotDiff`. 🧪
  - the ingestor writes the snapshot, its technologies, the denormalised site columns, `changes_count` and activity
- ☑ Snapshots API: list, get (`?raw=1`), diff, delete; `POST /sites/:id/fetch`.
- ☑ Taxonomy API (categories and tags, including merge) and saved views API.
- ☑ UI: **Add site dialog** with live normalisation, duplicate and related hints, category, status, tags and "fetch now".
- ☑ UI: **Sites list** (card view included):
  - plain shadcn table (TanStack Table was not needed: sorting is server-side through the URL); no column visibility menu yet
  - filters bound to URL search params (zod schemas) 🧪, facet counts, sorting, pagination or infinite scroll
  - saved views in the sidebar; bulk action bar; inline status edit
- ☑ UI: **Site detail shell**: header (status, category and tag editors, re-fetch), tabs, sidebar.
- ☑ Design age clues (not in the original plan): `design.py`, schema, summary and a Profile section.
- ☑ Preview URL per site and an archived-sites view with restore.
- ☑ UI: **Profile sections** (PRD §5.3): status banner, overview strip, tech stack with evidence popovers, hosting/DNS/email, TLS, registration, SEO with gauges and OG preview, performance, pages and sitemap table, contact and social, security headers and cookies, history, limitations, plus the **Raw JSON** tab (JSON tree, search, copy, download). Copy buttons everywhere.
- ☑ UI: **History tab** + compare view (grouped changes, unchanged sections listed). The raw JSON line-diff toggle is not built; the Raw JSON tab per snapshot covers it for now.
- ☑ Fetch progress card with phase steps; partial and failed states; blocked-site messaging.
- ☑ `bin/seed.php --demo`: imports the 4 example JSONs as v1 snapshots, plus tags, a category and a saved view.
- ☐ 🪟 Add 3 real client sites; re-fetch one; compare; filter and bookmark a URL; bulk-change status.

---

## M4: Notes, to-dos, activity, dashboard, contacts and backup

- ☑ Notes API (with optimistic-concurrency `409`). UI: list, editor (Edit, Split and Preview; autosave; `Ctrl+S`; unsaved guard), pinned notes, search.
- ☑ `Markdown` component: react-markdown + remark-gfm + rehype-sanitize, safe links, copyable code blocks. 🧪 XSS snapshot tests.
- ☑ To-dos API (per site and global). UI: quick-add with `!high` and `due:` parsing, priority and due editing, done collapse, drag reorder (native HTML5 drag and drop, mouse only); global `/todos` page grouped by due bucket. 🧪 parser.
- ☑ Activity: `ActivityLogger` hooked into all mutations and job results; per-site timeline; global feed.
- ☑ Contacts and links API plus the sidebar UI (mailto, tel and WhatsApp actions; typed link icons).
- ☑ Dashboard API plus widgets (active by status, deadlines, open to-dos, expiring SSL and domain, recent fetches, jobs, activity).
- ☑ Backups: `POST /api/backups` (`VACUUM INTO`), list and download, retention; Settings UI. Taxonomy management UI.
- ☑ Site `deadline` field editing; list column and filter.
- ☐ 🪟 A day of real use: notes with tables and checklists, to-dos across 3 sites, the dashboard reflects them.

---

## M5: Crawler v2 and the content UI

- ☑ Crawler output layer (PRD §5.11):
  - run folder
  - `pages/` naming 🧪 and internal link rewriting 🧪
  - global SHA-256 asset store with readable names 🧪 and image dimension sniffing 🧪
  - `manifest.json`, `structure.json`, `assets.json` and `urls.json`, written atomically every 10 pages and at the end 🧪
  - agent-oriented `README.md`
- ☑ Limits and options:
  - `max_pages` for WordPress too, with lazy WordPress pagination
  - sitemap cap; total and per-file size caps; file-type filters
  - concurrency 1–4 with a global rate limiter; robots toggle
  - the netguard on every request and redirect
- ☑ Contract: `--spec`, NDJSON events (progress, stats, log), exit codes, cancel file + SIGTERM → flush and `status: canceled`. 🧪
- ☑ Keep the extraction core; add fixture regression tests for `render.py` (div-soup, tables, lazy images, `srcset`). 🧪
- ☑ PHP:
  - `CrawlIngestor` (crawl row and counts from the manifest)
  - `CrawlReader` (pages, page markdown, assets, tree)
  - `SafePath` 🧪; file streaming with safe content types
  - archive job (ZipArchive, cached)
  - `reveal` (`explorer.exe /select,` on Windows, `xdg-open` on Linux)
- ☑ Crawls API (PRD §8.3) with a concurrency limit of one crawl.
- ☑ UI:
  - Content tab: crawl list and new-crawl dialog (options remembered per site, regex validation, robots-off confirm)
  - progress panel (bar, counters, elapsed time, current URL, log tail, cancel; no ETA)
  - result browser: page tree and rendered Markdown, image gallery with lightbox and "used on", files table, structure view
  - ZIP download, open folder, copy path, delete
- 🪟 Crawl one real client site (≈100 pages): cancel midway → the folder is consistent; re-run to completion; ZIP; open folder in Explorer; point your coding agent at `README.md`.

---

## M6: Polish, hardening and docs (MVP release)

- ☑ Audit every view for empty, loading and error states; skeletons; `aria-live` for progress; focus management in dialogs. Every feature view handles loading and error; added a route error boundary, a pending state for lazy chunks, and live regions on crawl progress. Dialogs use Base UI, which traps and restores focus.
- ☑ Responsive pass (sidebar drawer, stacked table rows on mobile). Checked at 390 px: no page scrolls sideways and the drawer exists; the sites table scrolls inside its box.
- ☑ CSP and security headers; production error responses with `logRef`; check the log format is readable.
- ☑ Performance: route-level code splitting; list API timing with 1,000 seeded sites (target < 100 ms): 3 to 24 ms measured with `php -S`.
- ☐ README final (backup, restore and troubleshooting added; Windows section needs your pass): Windows setup (Python, uv, php.ini extensions, Appytizer junction, optional Task Scheduler daemon), Linux dev, build, backup and restore, troubleshooting (Health page).
- ☑ Upgrade notes: the migration of legacy `backend/data`; how to import existing `!Reports` (if chosen in Open Question 4).
- 🪟 Full regression checklist on Windows (below).

### Windows regression checklist (run at M0, M3, M5, M6)
- [ ] `pnpm build` twice → `data/app.sqlite` checksum unchanged; `dist/` contains no `data/`.
- [ ] Deep link refresh on `/sites/:id` works.
- [ ] The Health page is all green.
- [ ] A profile job completes; cancel works; a job survives php-cgi recycling (or the daemon is registered).
- [ ] A crawl with Turkish URLs and file names produces valid paths (< 260 characters, no reserved names).
- [ ] Open folder launches Explorer; ZIP downloads and opens.

---

## v1.1 (after MVP), in suggested order
1. Command palette (⌘K) + keyboard shortcuts (F19) + global FTS5 search (F20)
2. Card view (F7) + Kanban by status (F21)
3. Clients entity (shared contacts) + access checklist with references (F22)
4. Money: offers, invoices, payments + "Unpaid" widget (F23)
5. Milestones (F24) + reminders (F25)
6. Templates (F26) + attachments (F27)
7. Export and import + `!Reports` importer (F28)

## Later
Scheduled re-fetch and uptime (F29, a Task Scheduler CLI) · PSI/Lighthouse, broken links, SEO and accessibility audit (F30) · optional Playwright rendering and screenshots (F31) · optional encrypted vault (F32).
