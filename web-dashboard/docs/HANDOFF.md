# Handoff

Read this first when you continue in a new session. It records where the project stands, what is decided, what is still open, and exactly what to do next.

- **Branch:** `claude/wonderful-knuth-iru2fk`
- **Last updated:** 2026-10-26 (M0 session)
- **Read next, in this order:**
  1. [`PRD.md`](./PRD.md): the spec (approved)
  2. [`DECISIONS.md`](./DECISIONS.md): why things are the way they are
  3. [`IMPLEMENTATION_PLAN.md`](./IMPLEMENTATION_PLAN.md): milestones and checklists

---

## 1. Where we are

| Step | State |
| --- | --- |
| Findings on the existing scripts (PRD §1) | ✅ done |
| PRD, decisions, implementation plan | ✅ done, **approved by you** ("All looks good") |
| Frontend layout preview with routes and demo data (no backend) | ✅ done (this session) |
| M0: backend foundation and Windows spike | ✅ done and verified on Windows |
| M1: profiler v2 (Python) | ✅ built and tested on Linux. ⏭ **You run it on Windows** (§5) |
| M2: job runner | ✅ built and tested on Linux. ⏭ **You verify on Windows** (§5c) |
| M3: sites, profiles, list and detail | ✅ built and tested on Linux. ⏭ **You verify on Windows** (§5d) |
| M4: notes, to-dos, activity, dashboard, contacts, backups UI | ✅ built and tested on Linux. ⏭ **You verify on Windows** (§5e) |
| M5: crawler v2 and the content UI | ✅ built and tested on Linux. ⏭ **You verify on Windows** (§5f) |
| M6: polish, hardening, docs | ✅ built (code splitting, security headers, list timing, state audit, upgrade notes). ⏭ **You run the full Windows regression checklist** (IMPLEMENTATION_PLAN.md) |

M0 is implemented (see §5). The old `backend/api.php` demo and its `backend/data/` are deleted; the Vite plugin now copies the whole backend. The Python scripts themselves are **unchanged** (M1 and M5); only `scripts/pyproject.toml`, `uv.lock` and `scripts/tests/` were added.

## 2. Decisions confirmed by you

- The PRD as written, including the MVP / v1.1 / later split.
- **Keep the Python scripts** for both the profiler and the crawler, run as background jobs (D1, D2).
- **One uv project for both Python tools** (D11):
  - one `scripts/pyproject.toml` with the union of both tools' dependencies, one `uv.lock`, one `scripts/.venv`
  - shared code goes in `scripts/site_tools/` (NDJSON events, exit codes, the SSRF guard, the spec loader)
  - the dependencies today are `dnspython`, `tldextract`, `beautifulsoup4`, `lxml` and `httpx`, plus `defusedxml` (new) and `pytest` (dev)
  - **neither script has a dependency file yet**, so this is the first M0/M1 task on the Python side

- **The Python scripts are internal to this app, not CLI tools** (your instruction after M0). `site_profile.py` and the crawler may change freely, including flags and output, as long as the job contract (`--spec`, NDJSON events, exit codes) is kept. Backwards-compatible CLI flags are no longer a goal.
- **Windows M0 result:** verified by you on Appytizer (PHP 8.5.9 cgi-fcgi, project at `D:\Projects\Freelance\dashboard\app`): deep link works, Health is all green (the earlier "Host not allowed" came from a different folder/host name), and the 180 s spawn test **succeeded**, so on-demand spawning works and the Task Scheduler daemon is not needed. Cancel and the double `pnpm build` (data untouched, no `data` in `dist`) are verified too, so **M0 is complete**. The two `php.exe` processes seen during a job are the worker and its child; the worker lingers about 10 s after the job ends, by design. The Health "Legacy data" row is harmless; `php backend/bin/migrate.php --import-legacy` clears it.

## 3. Open questions (still unanswered)

You approved the PRD without answering PRD §13. Proceed with these defaults unless you say otherwise:

| # | Question | Default used until answered |
| --- | --- | --- |
| 1 | Python ≥ 3.11 and `uv` on your Windows PC? | Assume yes. Settings → Health will show it if not. |
| 2a | Does Appytizer fall back to `index.php` for unknown paths? | Build for history-mode URLs plus `dist/index.php`. If deep links 404, set `VITE_ROUTER_MODE=hash` (already wired in `src/router.tsx`). |
| 2b | `php.exe` next to `php-cgi.exe`? Which PHP version? | Assume yes, PHP ≥ 8.1. It is configurable via `APP_PHP_CLI`. |
| 2c | Does php-cgi run as your Windows user? | Assume yes. Verified by the M0 spawn spike. |
| 3 | Data location | `<project>\data` (outside `dist/`) |
| 4 | Import `D:\Projects\Freelance\!Reports\*.json`? | No for MVP. The importer stays in v1.1 (F28). The demo seed uses `scripts/examples/*.json`. |
| 5 | Meaning of categories / seed list | Start empty in production. The demo seed uses "Food & agriculture, Industry, Energy, Retail". |

## 4. The layout preview (what was built this session)

### Run it
```bash
pnpm install
pnpm dev            # http://localhost:5173. No backend needed: all data is in src/mocks/
pnpm lint && pnpm typecheck && pnpm build   # all green at handoff time
```

A blue banner on each page says it's a preview. Buttons that would call the API show a "Preview only" toast. Some interactions are local only: status select, to-do checkboxes, note drafts, filters.

### Routes (`src/router.tsx`, TanStack Router, code-based)

| URL | Component | Shows |
| --- | --- | --- |
| `/` | `features/dashboard/dashboard-page.tsx` | Status counts, deadlines, expiring SSL/domains, open to-dos, recent fetches, running jobs, activity |
| `/sites` | `features/sites/sites-page.tsx` | Table/cards toggle, search, multi-filters with facet counts, sort, active-filter chips, bulk bar. **All state is in the URL** (`?status=design,waiting&tag=seo&view=cards`). |
| `/sites/$siteId` | `features/site/site-layout.tsx` + `profile-tab.tsx` | Header (status, category, tags, actions), tab links, right sidebar (deadline, contacts, links, health). Profile tab: partial banner, 8 stat cards, collapsible sections (tech with evidence popovers, hosting/DNS/email, TLS + domain, SEO with gauges, performance, sitemap URLs, contact/social, security, history), and a Raw JSON toggle. |
| `/sites/$siteId/notes` | `notes-tab.tsx` | Note list, Edit/Split/Preview, sanitized Markdown |
| `/sites/$siteId/todos` | `todos-tab.tsx` | Quick-add input, open list, collapsible done list |
| `/sites/$siteId/content` | `content-tab.tsx` | Crawl list, running-crawl progress panel with log, result browser (pages, images, files), new-crawl options dialog |
| `/sites/$siteId/history` | `history-tab.tsx` | Snapshot list plus grouped diff view |
| `/sites/$siteId/activity` | `activity-tab.tsx` | Timeline |
| `/todos` | `features/todos/todos-page.tsx` | All open to-dos grouped Overdue / Today / This week / Later / No date, filterable |
| `/jobs` | `features/jobs/jobs-page.tsx` | Job list with progress, cancel and retry |
| `/settings` | `features/settings/settings-page.tsx` | Health checks (shape of `/api/health`), backups, categories, tags |
| anything else | `NotFound` in `router.tsx` | |

Demo states worth knowing:
- site **1** (`forazeytin.com.tr`) has everything: a partial profile, notes, to-dos, a running crawl, history
- site **4** shows a **failed** fetch
- site **7** has **never been fetched** (empty state)

### File map

```
src/
├── main.tsx                     # QueryClient → ThemeProvider → TooltipProvider → RouterProvider + Toaster
├── router.tsx                   # all routes; VITE_ROUTER_MODE=hash switches to hash history
├── index.css                    # shadcn tokens + small .markdown styles (no typography plugin)
├── mocks/
│   ├── types.ts                 # UI types mirroring PRD §8.2 / §7.3 (ProfileSummary etc.)
│   ├── data.ts                  # demo data, dates relative to "now"
│   └── store.ts                 # read helpers (getSite, categoryOf, tagsOf, forSite…)
├── lib/
│   ├── format.ts                # relativeTime, shortDate, daysUntil, dueLabel, formatBytes
│   └── site-filters.ts          # zod search schema for /sites + client-side filter/sort stand-in
├── components/
│   ├── ui/                      # shadcn (base-nova on Base UI), added with `pnpm exec shadcn add …`
│   ├── layout/app-shell.tsx     # sidebar (nav + saved views), top bar (search, jobs, theme, Add site), mobile sheet
│   └── common/                  # badges.tsx, misc.tsx (CopyButton, PageHeader, EmptyState, KeyValue,
│                                #   StatCard, PreviewNotice), markdown.tsx, simple-select.tsx
└── features/
    ├── dashboard/  sites/ (sites-page, add-site-dialog)  site/ (layout + 6 tabs)
    ├── todos/ (todos-page, todo-list shared)  jobs/  settings/  activity/ (activity-feed shared)
```

### Conventions and gotchas (learned while building it)

- **Swapping mocks for the API (M3):** components import only from `@/mocks/store` and `@/mocks/types`.
  - Replace the store helpers with TanStack Query hooks in `src/api/` (PRD §10.1), keeping the same type names.
  - `filterSites()` in `lib/site-filters.ts` is the client-side stand-in for the server's `SiteQuery`. Keep the zod schema and drop the filtering when `/api/sites` exists.
- **Route data access:** pages use `getRouteApi("/sites/$siteId")` / `getRouteApi("/sites")` instead of importing route objects, which avoids circular imports with `router.tsx`. `siteId` is parsed to a number in the route's `params.parse`.
- **Links styled as buttons:** use `<Link className={buttonVariants({...})}>`. Don't use `<Button render={<Link/>}>`, because Base UI's Button expects a native button.
- **Dropdown triggers** (`DropdownMenuTrigger`) already render a `<button>`, so style them with `buttonVariants(...)`.
- **Select:** Base UI needs the `items` prop to show the selected label. Use `components/common/simple-select.tsx`.
- **`cn` comes from the npm package `cn`.** It is shadcn's own clsx + tailwind-merge replacement, so don't swap it out.
- **shadcn `sonner.tsx`** was generated importing `next-themes`. It was patched to use our `@/components/theme-provider`; re-apply the patch if you regenerate it.
- **lucide-react v1 has no brand icons** (Facebook, Instagram, GitHub…). Some names also differ: `Funnel` (not `Filter`), `Trash` (not `Trash2`), and there is no `History`. Check with `grep "declare const Name:" node_modules/lucide-react/dist/lucide-react.d.ts`.
- **Colours:** status, outcome and job badges use `text-*-700 dark:text-*-300` so both themes work. Dark is the default.
- **`pnpm typecheck`** now runs `tsc -b`. The old `tsc --noEmit` checked nothing, because the root tsconfig has `files: []`.
- **The bundle is ~960 KB (300 KB gzip)**, so Vite warns about chunk size. Route-level `lazy()` code splitting is planned for M6.
- `@tanstack/react-table` is **not** installed yet. The preview uses the plain shadcn table. Add it in M3 for server-side sort and selection.

## 5. M0 state and next steps

### What M0 delivered

- **Python:** `scripts/pyproject.toml` (one uv project, Python ≥ 3.11, `beautifulsoup4 defusedxml dnspython httpx lxml tldextract`, `pytest` dev), `uv.lock`, `scripts/tests/test_smoke.py`. Both tools still run (checked with `example.com` and `books.toscrape.com`).
- **Backend** (`backend/`, PHP ≥ 8.1, no runtime deps):
  - `bootstrap.php` (PSR-4 autoload, errors become exceptions), `index.php` (API entry), `dev-router.php`
  - `src/Infra/`: `Config` (env, then `config.local.php`, then OS defaults; refuses a data dir inside the web-served build), `Paths`, `Platform`, `Database` (WAL, FKs, busy timeout), `Logger`, `Migrator`, `Backup` (`VACUUM INTO`, keeps 20 automatic)
  - `src/Http/`: `Router`, `Request`, `Response`, `Guard` (Host allow-list, JSON plus `X-Requested-With`, Origin check), `HttpException`
  - `src/App.php`: service container, route table, `handle()` (guard, auto-migrate, error to `logRef`)
  - `migrations/0001_init.sql`: the full MVP schema from PRD §7.2
  - `bin/migrate.php [--status|--import-legacy]`, `bin/worker.php [--daemon]`, `bin/selfcheck.php` (the spike's child process)
  - `src/Jobs/`: `JobRepository` (atomic claim with per-type limits), `Worker`, `JobLauncher::ensureWorker()` (lock probe, orphan recovery), `Process/{PosixProcess,WindowsProcess}`
  - Endpoints: `GET /api/health`, `GET|POST /api/backups`, `POST /api/system/spawn-test`, `GET /api/jobs`, `GET /api/jobs/:id`, `/log`, `POST /cancel`, `POST /retry`
- **Front controller and build:** `public/index.php` (lands in `dist/index.php`), a Vite plugin that copies `backend/` minus tests, vendor, data and dev files, writes `build-info.php`, and aborts if `APP_DATA_DIR` is inside `dist/`. The dev proxy is `/api` to `PHP_API_TARGET` (default `http://127.0.0.1:8787`).
- **Frontend:** `src/api/{client.ts,types.ts,hooks/system.ts}` (the `X-Requested-With` header is added there). Settings → Health, the worker spawn test and Backups use the real API. Categories and tags on that page are still demo data.
- **Tooling:** `pnpm dev:api`, `dev:worker`, `migrate`, `test` (= `test:web` Vitest, `test:php` PHPUnit 11, `test:py` pytest). 17 PHPUnit tests (router, guard, migrator including backup and rollback, paths, config, and an HTTP integration test that boots `php -S` and runs and cancels real jobs), 5 Vitest, 1 pytest.
- **README** rewritten for Linux and Windows.

### Design notes worth knowing

- **Job output goes to files, not pipes.** The worker starts the child with `proc_open` writing stdout to `data/jobs/<id>/events.ndjson` and stderr to `job.log`, then tails the events file. Windows pipes cannot be read without blocking, so this works the same on both OSes.
- **Soft cancel is a `cancel` file** in the job folder, on both OSes. The child polls it. On Linux the worker also sends SIGTERM to the process group (the child is started through `setsid`). After 5 s it hard-kills (`taskkill /T /F` on Windows).
- **Worker liveness is the `data/run/worker.lock` flock.** A free lock means no worker, so orphaned `running` jobs (stale heartbeat) are marked failed.
- `PDO::inTransaction()` does not see a manual `BEGIN`, so the migrator rolls back unconditionally on failure.
- `composer install` is slow in the cloud container (the GitHub zip downloads are blocked, it falls back to git clones). Allow a few minutes and run it in the background.

### 🪟 What you do on Windows (M0 verification)

```
git pull
pnpm install
uv sync --project scripts
(cd backend && composer install)      # optional, only for PHP tests
pnpm build
```

Then check:
1. `http://dashboard.test/sites/1` loads the app. If it 404s: the server has no `index.php` fallback. Rebuild with `VITE_ROUTER_MODE=hash` and tell me (Open question 2a).
2. Settings → Environment health is all green. If PHP CLI is red, set `APP_PHP_CLI` or `php_cli` in `config.local.php` (copy `backend/config/config.example.php`). If Python is red, run `uv sync --project scripts`.
3. Settings → "Worker spawn test" → Run test. The progress must keep counting for the full 3 minutes (reload the page in between, and wait for at least one php-cgi recycle if you can). A `php.exe` process should be visible in Task Manager. Also try Cancel on a second run: it should end as `canceled` within about 6 s.
4. Run `pnpm build` twice. `data\app.sqlite` must be unchanged and there must be no `data` folder under `dist`.
5. If (3) fails, send me what the job row shows (`/api/jobs/<id>`) and `data\logs\worker-*.log`. The fallback is the Task Scheduler daemon described in the README.

## 5b. M1 state (profiler v2) and next steps

### What M1 delivered

- **`scripts/site_tools/`** (shared with the crawler later): `events.py` (thread-safe NDJSON `Emitter`), `exitcodes.py`, `cancel.py` (`CancelToken`: cancel file or SIGTERM), `spec.py` (`--spec` loader), `netguard.py` (SSRF guard: every resolved address must be globally routable, ports 80/443/8080/8443, multicast blocked).
- **`scripts/site_profile/`** (run as `python -m site_profile`; the old `site_profile.py` is deleted):
  - `core.py` orchestrates: resolving, http, page, then dns/hosting, tls, registration, sitemaps, archive and the http checks in parallel. A broken section becomes an `errors[]` entry, never a crash. Status is `ok`, `partial` (any error, including a Wayback timeout) or `failed` (site unreachable or NXDOMAIN). Exit codes 0/1/3, 2 for bad input, 4 for blocked, 130 for cancelled.
  - `http.py` (httpx): redirects followed by hand so each hop is recorded and SSRF-checked; TTFB and total time; certificate failures are retried unverified and flagged (`http.checks.tls_verified=false`).
  - `dnsinfo.py` (3 parallel queries, 2 attempts, optional resolver), `hosting.py` (IPs, reverse DNS, ASN through Team Cymru DNS, email provider from MX), `tls.py` (reads the certificate even when verification fails), `registration.py` + `whois.py` (RDAP, then WHOIS; TRABIS block parser), `page.py` (metadata, headings, hreflang, OG/Twitter, links, favicon, consent tool, contacts, socials, WhatsApp, WordPress version/theme/plugins), `sitemaps.py` (defusedxml, DOCTYPE stripped, index followed, caps 10 files / 50k counted / 500 kept, gzip), `archive.py` (Wayback, 6 s), `tech.py` (adds a Wix/builder false-positive rule and a Consent category), `schema_v2.json`.
  - Raw report keys (what the M3 `ProfileNormalizer` reads): `schema_version, status, errors[], timings_ms, metadata, input, http, page, contacts, wordpress, domain_registration, website_history, dns{domain_records, website_host_records, ns{values,source}}, email{provider,mx[]}, hosting, tls_certificate, robots_and_sitemaps{robots, sitemaps{files,total_urls,urls,truncated}}, technologies, limitations`. Sections keep their full key set when they fail (`None` or empty), and `schema_v2.json` is checked in tests.
- **Job mode:** `python -m site_profile --spec data/jobs/<id>/spec.json` reads `params.url` (plus optional `timeout`, `allowPrivate`, `resolver`), prints only NDJSON events (`progress` x9, `result {path,status,errors,duration_ms}`), writes `profile.json` next to the spec (atomically), and stops on the spec's `cancelFile`. The process exits with `os._exit` after the report is written because network threads may still sit in a socket timeout.
- **Tests:** 58 pytest tests, fully offline (netguard, events, spec, cancel, parsers on fixtures, whole-report logic with the network sections mocked, CLI exit codes, schema validation, the 4 real v1 JSONs kept as goldens in `scripts/tests/fixtures/v1/`). `jsonschema` is a new dev dependency.

### Things to know

- The `.tr` WHOIS fixture is a real TRABIS record (registrant anonymised).
- In the cloud container, Wayback and Cymru DNS are unreachable and NS answers are empty, so `partial` with an `archive` error is normal there. The registry NS fallback was exercised that way.
- `Worker::command()` does not run the profiler yet (the `profile` job type is wired in M3 with the ingestor). When wiring it: command is `[python, '-m', 'site_profile', '--spec', specPath]` and it must run with **cwd = `<project>/scripts`** (add a `cwd` to `Worker::start()`'s `proc_open`). A failed profile still produces `profile.json` and a `result` event, so the worker/ingestor should read the result even for exit codes 3 and 4.
- Exit code 1 means `partial`, which the worker already maps to job status `partial`.

### Windows M1 result (verified by you)

`forazeytin.com.tr` ran in ~10 s: DNS A/MX/TXT/SOA/DMARC, hosting (AS34619, reverse DNS, prefix), Sectigo TLS, WHOIS (created 2000-07-06, expires 2027-07-05, nameservers) and Wayback (first capture 2003) all worked, and all 58 pytest tests pass on Windows. Two findings were fixed afterwards: the site's `sitemap.xml` is not valid XML (now read leniently, with a `warning` on the file entry), and your router never answers NS queries (the registry nameservers are used, and that no longer counts as an error). `.tr` `last_updated` is ignored because TRABIS only reports its own database timestamp. The `.tr` registrar (ODTÜ Geliştirme Vakfı) is correct: it is the accredited registrar of record, and Natro is only the host. The TRABIS fixture is now the real record (anonymised), and the parser matches it.

### Registration additions (after your review)

`domain_registration` now also has `registrant` (the owner named in the registry, skipped when the registry hides it), `transfer_status` (raw text) and `transfer_locked` (`true`, `false` or `null` when unknown). The lock comes from the TRABIS "Transfer Status" line, or from EPP statuses such as `clientTransferProhibited`; a bare "Active" gives `null`. For M3: `ProfileNormalizer` maps these to `domain.registrant` and `domain.transferLocked` (PRD §7.3), the Profile tab shows a lock badge, and the dashboard's sites list could filter on it later.

### 🪟 What you do on Windows (M1 verification, done)

```
git pull
uv sync --project scripts
cd scripts
uv run python -m site_profile forazeytin.com.tr --events --output x.json
uv run pytest
```

Check in `x.json`: `dns.ns.values` is filled (and `dns.ns.source`), `domain_registration` has `registrar` and `expiration_date` for the `.tr` domain, `hosting.asn` is set, `page.contacts` look right, `errors` only lists real problems. Also try a client site with a bad certificate, and `http://localhost` (must exit 4: blocked). Send me anything that looks wrong.

## 5c. M2 state (job runner) and next steps

### What M2 delivered

- **Worker** (`backend/src/Jobs/Worker.php`): a per-type command table with a working directory (`profile` is prepared but commented out until M3), options for tests (`idleExitAfter`, `hardKillAfter`, `timeouts`, a command resolver), a 10 MB cap on worker-written log lines, deletion of `events.ndjson` after a job ends, and job-folder pruning on start (`JobMaintenance`, finished jobs older than 30 days; rows stay, `log_path` becomes null).
- **Tests:** `WorkerTest` runs the real worker against `tests/fixtures/fake_job.php`: success (progress, stats, result, non-JSON lines to the log), partial (exit 1), crash (exit 3, stderr kept), cancel while running, timeout, unimplemented type, a held lock, orphan recovery, a fresh heartbeat not being an orphan, and pruning. PHPUnit is now 27 tests.
- **UI** (real API, no demo data): `src/api/hooks/jobs.ts`, `features/jobs/jobs-page.tsx` (All/Active/Finished filter, progress, cancel, retry, click a row for details), `job-drawer.tsx` (status, progress, stats, error, live log tail with auto-scroll and copy), `job-watcher.tsx` (a toast when a job leaves the active list: finished, finished with problems, canceled, failed), and a top-bar indicator ("2 jobs") fed by the same poll. Polling is 2 s while something is active and 10 s otherwise, and each poll lets the API start a worker if jobs are queued.
- The dashboard page's "running jobs" widget and the site Content tab still show demo data until M4/M5.

### 🪟 What you do on Windows (M2 verification)

```
git pull
pnpm install
pnpm build          # then reload http://dashboard.test
```

1. Settings → Run test twice, then open **Jobs**. Both should show as running with progress, and the top bar should say "2 jobs".
2. Click a row: the drawer shows a live log line and the progress bar. Cancel one: it ends as `canceled` within about 6 s and a toast appears. Let the other finish (3 min) and check the "finished" toast.
3. Start a test, then close the browser tab for a minute and open it again: the job kept running.
4. Restart the machine (or kill the worker `php.exe` and its child in Task Manager) during a test, then open Jobs: the job becomes `failed: Worker stopped unexpectedly` within about 30 s, and Retry queues it again.

## 5d. M3 state (sites, profiles, list and detail) and next steps

### What M3 delivered

- **Backend** (`backend/src/Domain`, `Controllers`):
  - `PublicSuffixList` (bundled `resources/public_suffix_list.dat`, ICANN section only), `UrlNormalizer` (same rules as the profiler: IDN, www-less site key, ports, no userinfo), `NetworkGuard` (private, loopback, link-local, CGNAT, multicast, v4-mapped v6).
  - `ProfileNormalizer` (schema v1 and v2 to the PRD §7.3 summary, plus `facts()` for the `sites` columns), `SnapshotDiff`, `ProfileIngestor` (snapshot, technologies, site facts, change count, activity). A failed attempt is stored but `sites.latest_snapshot_id` keeps pointing at the newest snapshot with data, so an outage does not blank the profile; `lastAttempt` on the site detail tells the UI.
  - `SiteQuery` (filters, facet counts that ignore their own filter, whitelisted sorts with never-fetched last, pagination), `SiteService`, `SitesController` (list, check-url, create, get, patch, archive/restore/delete with typed confirm, bulk, fetch), `SnapshotsController` (list, get with `?raw=1`, diff, delete), `TaxonomyController` (categories, tags with merge, saved views).
  - The worker now runs `profile` jobs (`python -m site_profile --spec`, cwd `scripts/`) and `App::onJobFinished` ingests `data/jobs/<id>/profile.json`, or stores a failed snapshot when there is no usable report. A profile job that ended `failed` with a report gets the profiler's first error as its job error.
  - `php backend/bin/seed.php --demo` loads the four example profiles (as v1 snapshots), four categories, tags and two saved views. Idempotent.
- **Tests:** PHPUnit is now 89 tests (domain/PSL/normaliser/guard/diff/ingest unit tests, plus HTTP tests for sites, filters, facets, bulk, taxonomy, snapshots and the fetch 409 rule). Vitest has 8.
- **Frontend (real API):** `src/api/hooks/sites.ts`, Add site dialog (live check-url with duplicate, related and LAN hints), Sites list (URL-driven filters with server facet counts, sort, pagination, table/cards, bulk bar, Save view dialog), saved views in the sidebar, site header (status, category, tags, edit details, archive, delete with typed confirm, re-fetch) with a live fetch banner, full Profile tab on the new summary (registrant, transfer lock, ASN, WordPress theme/plugins, OG preview, sitemap warnings, Raw JSON tab with filter, copy and download), History tab with real diffs, Settings categories and tags (add, rename, merge, delete).
- **Not connected yet (as of M3, fixed in M4 except Content):** see §5e.
- **Known gaps:** no column-visibility menu, no inline status editing in the table, no raw-JSON line diff in History, no "use as crawl seed" action on sitemap URLs (M5).

### Fixes and additions after your first M3 test

- **Profiler:** `<img>` inside `<noscript>` crashed page parsing on `resoltenerji.com` (counts are now taken before the tree is pruned). TLS verification now trusts certifi's bundle and the OS store, because Windows' store lacks newer roots (the `forazeytin` "unable to get local issuer" warning). A sitemap that cannot be parsed is now a warning on that file, with the first 160 characters, instead of a profile error. Reserved and local names (`.test`, `.local`, hosts-file entries) skip the registry lookup, and a name that is missing from public DNS but served over HTTP is no longer a failure. `fora-react.test` should now come out `ok` if your machine serves it.
- **Design age clues** (`site_profile/design.py`, key `design` in the report and summary): oldest and newest WordPress upload paths (`/wp-content/uploads/YYYY/MM/`), other dated asset paths, `?ver=` timestamps, `Last-Modified` of up to six same-site files (stylesheets first), the footer copyright year, plus context rows (first Wayback capture, domain registration, sitemap `lastmod`). Shown as the "Design age" section and a "Design dated" card. They are hints: the estimate is the oldest and newest *dated asset*, not a build date.
- **Preview URL:** new `sites.preview_url` column (migration 0002), set from the "Preview URL" button next to the open-in-browser icon (accepts `my-site.vercel.app`, adds `https://`). Once set the button becomes "Preview" with a pencil to edit or remove it.
- **UI:** deleting a site only asks for confirmation (no typing). Archived sites: use the "Archived" button on the Sites page, select rows and "Restore" (also a bulk action in the API); archiving shows an "Undo" toast. `?q=2024`-style numeric URL filters no longer get dropped by the search schema.
- Snapshots are re-normalised from their raw report when read, so profiles stored before a summary field existed still get it.

### 🪟 What you do on Windows (M3 verification)

```
git pull
pnpm install
pnpm build          # migrations run on the first request; reload http://dashboard.test
php backend/bin/seed.php --demo      # optional: demo sites. Skip it to start with an empty list.
```

1. Add three real client sites (Add site). Each should land on its page with a "Fetching profile…" banner and fill in by itself in 10 to 40 s. Check the Profile tab (registrar, registrant, transfer lock, hosting, email provider, sitemap).
2. Re-fetch one site, then open History: a second snapshot with "N changes" or "no changes".
3. Try a duplicate (`www.` of an existing site) and a LAN address (`http://192.168.1.1`): both must be refused with a clear message, and the LAN one allowed after ticking the box.
4. Filter `status`, `tag` and `CMS`, copy the URL, open it in a new tab: same list. Save the view and see it in the sidebar. Select two rows and set a status in bulk.
5. Archive a site and restore it; delete one with the typed confirmation.

## 5e. M4 state (notes, to-dos, activity, dashboard, contacts and links) and next steps

### What M4 delivered

- **Backend:** `NotesController` (excerpt list with server-side search, pin, optimistic concurrency: a save must send the `updatedAt` it last saw or gets `409` with the current note; edits are logged once per 30 minutes), `TodosController` (per-site and global lists with `status`, `site`, `priority`, `due` filters and a `today` parameter so "overdue" follows your calendar day; completing sets `doneAt`; `sortOrder` is a REAL so reordering writes one row), `ActivityController` (keyset pagination with `before`/`nextBefore`), `ContactsController` (contacts and typed links, validated), `DashboardController` (status counts, deadlines in 14 days, open to-dos due within 7 days, SSL within 30 and domains within 60 days, recent fetches, jobs of the last day, activity), backup download (`/api/backups/{name}/download`, name checked against a strict pattern), `Response::file`. Jobs now carry `siteKey`.
- **Tests:** PHPUnit is 96 tests (new `WorkApiTest`: notes concurrency and search, to-do lifecycle, ordering and global filters, activity paging, contact and link validation, dashboard, backup download traversal). Vitest has 18 (quick-add parser, Markdown list helpers, search schema, API client).
- **Frontend:** Notes tab (list with search, new note, split editor with autosave after 1.5 s, Ctrl+S, Tab indent, list continuation, unsaved-changes warning, conflict banner with "Load their version" / "Keep mine", pin, delete), To-dos tab (quick add with `!high`, `!low`, `due:fri|tomorrow|+3d|2026-12-24`, edit dialog, delete, drag to reorder open items, done list collapsed), global To-dos page grouped Overdue / Today / This week / Later / No date, Activity tab with "Load more", contacts and links with add, edit and delete in the site sidebar, Dashboard on live data, backup Download buttons in Settings. The old preview data is gone: `src/mocks/` and the preview Content tab are deleted (the Content tab layout is in git history, commit `fd77c84`, for M5). Shared constants live in `src/lib/domain.ts`.
- **Known gaps:** reordering is mouse-only (no keyboard alternative yet), a to-do's note is edited only in its dialog, no per-contact notes field in the UI (the API has it), backups are never pruned automatically except the pre-migration ones.

### UI feedback after the first M4 test

Notes open in Preview (Edit when empty), Markdown is coloured, the save state sits in a fixed slot at the bottom right, open to-dos have up/down buttons, contacts show plain text with copy buttons (the copy button now also works on non-secure `http://` origins), Dashboard deadlines are urgency cards. Favicons are served through `GET /api/sites/{id}/favicon` (fetched server-side with certificate checks off, every redirect hop checked against the network guard, content sniffed to PNG/JPEG/GIF/ICO/WebP/SVG, cached for 7 days in `data/favicons`) because the browser could not load them straight from sites with an invalid certificate, an http-to-https redirect or hotlink protection. The sites list and site header show them, falling back to the first letter.

### 🪟 What you do on Windows (M4 verification)

```
git pull
pnpm install
pnpm build          # reload http://dashboard.test
```

1. Open a site, Notes: create a note, type a table and a task list, wait for "Saved", reload the page: the text is there. Open the same note in a second tab, edit in both: the second save shows the conflict banner.
2. To-dos: add `Send invoice !high due:fri` and `Call Ali due:tomorrow`, drag one above the other, tick one off, open the Dashboard: the open one is listed with its date.
3. Contacts: add a contact with an email and phone, use the mail and WhatsApp buttons. Links: add a repo link.
4. Dashboard: set a deadline a few days ahead on a site (Edit details), it appears under Upcoming deadlines.
5. Settings → Backups: back up now, then Download and open the file with any SQLite viewer.

## 5f. M5 state (crawler v2 and the Content tab) and next steps

### What M5 delivered

- **Python (`scripts/site_crawl/`, replaces `generic_crawler`, which is deleted):** the extraction core (`render.py`) is kept; everything around it is new. Output per run: `pages/<slug>.md` (path segments joined by `--`, `pages/<lang>/` for language prefixes, `_con` style names for Windows reserved words), `images/` and `files/` de-duplicated by SHA-256, `manifest.json`, `structure.json`, `assets.json`, `urls.json`, an agent-facing `README.md` and `logs/crawl.log`. The JSON files are written atomically every 10 pages and at the end; internal links between exported pages (and to downloaded documents) are rewritten to relative paths in a final pass. `maxPages` also limits WordPress REST items (lazily paged). Total and per-file size caps stop the crawl gracefully (`partial`, `stop_reason: size_cap`). The start URL is always crawled first, robots.txt is respected unless switched off, and the network guard runs on every redirect hop. Contract: `--spec` with `params`, `cancelFile`; NDJSON `progress`/`stats`/`log`/`result`; exit codes 0 done, 1 partial, 3 unreachable, 4 blocked, 130 canceled. 13 new tests with a local test web server (83 Python tests in total).
- **Backend:** `CrawlStore` (options validation with regex checks, folder `data/crawls/<site-key>/<YYYYMMDD-HHMMSS>`, one crawl at a time → `429`, ingest from `manifest.json` when the job ends, delete), `CrawlReader` (pages with search and URL tree, page Markdown without front matter, assets with filters, raw file lookup), `SafePath` (no absolute paths, `..`, backslashes, NUL, drive letters or symlinks; realpath prefix check), `CrawlsController` (PRD §8.3: create, list, get, pages, page, assets, raw, archive build and download, reveal, delete), `bin/archive.php` (ZipArchive, cancel-aware, cached in `data/archives`), `Response::inlineFile` (every file is served with `nosniff` and `Content-Security-Policy: sandbox`; HTML and unknown types are downloads, SVG renders only as an image). `Worker::command()` now knows `crawl` and `archive`. Deleting a site deletes its crawl folders. PHPUnit is 110 tests (new `CrawlsApiTest` runs a real crawl of a tiny local site through the API, worker and Python; `SafePathTest`).
- **Frontend:** Content tab (`src/features/site/content/`): crawl list with status, new-crawl dialog (options of the last crawl prefill it, live regex check, "skip cart, search, tags" preset, extra confirmation to ignore robots.txt), progress panel (bar, counters, elapsed time, current URL, log tail with level filter, cancel), result browser (page tree or search list with rendered Markdown and local images, image gallery with lightbox, files table, structure summary with links to the index files), Download ZIP (builds on first use), Open folder, copy path, delete.
- **Known gaps:** no ETA in the progress panel, the image gallery loads at most 200 images (no paging yet), the structure view shows counts and file links rather than a JSON tree or link graph, "Open folder" only works when the PHP process runs on the same desktop (the path copy button is the fallback).
- `scripts/generic_crawler/examples/*.json` (the demo-seed profiles) moved to `scripts/examples/`.

### 🪟 What you do on Windows (M5 verification)

```
git pull
cd scripts && uv sync && cd ..
pnpm install
pnpm build          # reload http://dashboard.test
```

1. Open a client site, Content tab, **New crawl** with 30 pages. Watch the progress and the log. Cancel midway: the status becomes `canceled` and the folder is still readable (open `README.md`).
2. Crawl again to completion. Check Pages (tree, rendered Markdown with images), Images (lightbox shows alt text and the pages that use it), Files, Structure.
3. **Download ZIP**, then **Open folder** (Explorer should open). Point your coding agent at the folder's `README.md`.
4. Crawl a WordPress site with Max pages 10: it must stop at about 10 pages, not load the whole site.
5. Try a regex like `(` in "Skip URLs": the dialog must refuse it. Untick robots.txt: it must ask you to confirm.

### Next steps for the agent (M6, polish, hardening and docs)

The MVP milestones are done. Remaining: your Windows regression checklist (IMPLEMENTATION_PLAN.md, M6), then pick from v1.1. Known small gaps: image gallery paging, crawl ETA, a structure JSON tree view, keyboard reordering of to-dos.

## 6. Environment notes (cloud dev container)

- Node 22, pnpm 10, PHP 8.3 CLI (`pdo_sqlite`, `sqlite3`, `zip`, `intl`, `mbstring`, `curl`, `openssl`), Python 3.13, `uv`. Playwright and Chromium are pre-installed for screenshots: `require('playwright')` works with no install.
- `scripts/.venv` now exists in the project (gitignored). `backend/vendor` and the root `data/` are gitignored too.
- The container's network blocks or resets Wayback (`web.archive.org`), and NS answers came back empty. Don't treat those as script bugs when testing here. The NS timeout seen in your **real** Windows samples is a genuine issue (PRD §1.2 item 4).
- Useful test targets: `https://books.toscrape.com/` (crawler), `https://self-signed.badssl.com/` (TLS failure), `https://wordpress.org/news/` (WordPress, large; always use `--max-pages`).

## 7. Prompt to start the next session

> Read `docs/HANDOFF.md`, then `docs/PRD.md`, `docs/DECISIONS.md` and `docs/IMPLEMENTATION_PLAN.md`. Continue with milestone M6 starting at "Next steps for the agent (M6, ...)" in the handoff (and tell me the Windows M3, M4 and M5 results first, if I have them). Work on branch `claude/wonderful-knuth-iru2fk` (or the branch your session assigns), keep lint/typecheck/build green, update the handoff when you stop.
