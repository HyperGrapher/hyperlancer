# Decisions

This file records every "you decide" item from the brief, plus the supporting decisions they force. Each entry gives the options, the choice, and the reasoning. Status: **proposed**, to be confirmed together with the [PRD](./PRD.md).

| # | Decision | Choice |
| --- | --- | --- |
| D1 | Profiler: PHP rewrite or Python | **Keep Python** (refactor to v2 contract), run as a background job |
| D2 | Crawler: PHP rewrite or Python | **Keep Python** (rework the output layer), run as a background job |
| D3 | Profile storage | **Hybrid:** raw and normalised JSON in SQLite, filter facts as real columns; crawl files on disk |
| D4 | Job runner | **SQLite `jobs` table + on-demand detached PHP CLI worker**, with a Task Scheduler daemon fallback |
| D5 | UI stack and libraries | Keep the existing **React 19 + Tailwind 4 + shadcn (Base UI) + TanStack Query**; add TanStack Router and Table |
| D6 | Markdown library and editor | **react-markdown + remark-gfm + rehype-sanitize**; textarea split editor (CodeMirror later) |
| D7 | Credentials handling | **References only** (no secrets stored); optional encrypted vault later |
| D8 | Backend structure | **No framework**; a ~150-line router, PSR-4 autoloader, zero runtime Composer deps |
| D9 | URL layout and SPA routing | Root `index.php` front controller: `/api/*` goes to PHP, everything else to `index.html` |
| D10 | Data location and build safety | `<project>/data`, located via `build-info.php`; the build refuses to put data in `dist/` |
| D11 | Python environment | **One uv project** in `scripts/` for both tools (`pyproject.toml` + `uv.lock`, `.venv`). Confirmed by you. |
| D12 | CSRF / host safety model | Custom header + JSON-only + Host/Origin allow-list; no cookies |
| D13 | Snapshot diff | Server-side semantic diff over normalised summaries, with a raw diff fallback |
| D14 | Testing tools | Vitest, PHPUnit (dev-only Composer), pytest with offline fixtures |

---

## D1. Site profiler: rewrite in PHP or keep Python?

**Options**

| | A. Keep Python, call it from PHP | B. Rewrite in PHP | C. Hybrid: PHP for fast parts, Python for the rest |
| --- | --- | --- | --- |
| Windows reliability | Needs Python plus packages installed. With `uv`, the interpreter path is explicit and doesn't depend on PATH. | No extra runtime. | Worst of both. |
| Dependency burden | Python 3.11 + 3 packages (via `uv.lock`) | PHP ext only. But **DNS is weak**: `dns_get_record()` has no timeout control, no DNSKEY/CAA on many Windows builds, and no choice of resolver. WHOIS, TLS (`stream_socket_client` + `capture_peer_cert`), RDAP and HTML parsing (`DOMDocument`, poor on broken HTML) are all doable. | |
| Speed | Same network-bound time (10–40 s), plus ~0.3 s interpreter start | Same network-bound time | |
| php-cgi timeouts | Must run in the background either way: 10–40 s is too close to `max_execution_time`, which is wall-clock on Windows, and to `fastcgi_read_timeout` | **Also** needs a background job for the same reason | |
| Maintainability | Already ~1,200 working, evidence-based lines. You and your agents already work in it. BeautifulSoup is robust on real-world HTML. | ~1,500 new PHP lines and a re-implementation of tldextract (PSL), dnspython features and RDAP. High risk of regressions. | Two codebases for one feature. |
| Shared code with the crawler | Same language: shared SSRF guard, events, URL utils | Crawler would stay Python anyway (D2), so the duplication goes the other way | |

**Choice: A. Keep Python and refactor it to a v2 contract.** The deciding factor is that **a background job is required whatever the language**, so B buys only "no Python install". B costs weaker DNS, a full rewrite and two implementations of shared logic. Running locally makes the install a one-time `uv sync`.

**Changes required to `site_profile.py`** (see PRD §5.4):
- **CLI contract:**
  - `--spec FILE` (JSON inputs) or a positional URL
  - `--output FILE`; `--events` (NDJSON on stdout)
  - `--json` prints only JSON
  - remove the hard-coded `D:\…\!Reports` default; no file is written unless `--output` is given
- **Schema v2:**
  - `schema_version`, `status`, `errors[]`, `timings_ms`
  - every section always has its full key set
  - a JSON Schema file `scripts/site_profile/schema_v2.json` is used in tests
- **Exit codes:** `0` ok, `1` partial, `2` input, `3` unreachable, `4` blocked by policy, `130` cancelled.
- **Progress:** one event per phase (`http`, `page`, `dns`, `tls`, `registration`, `sitemaps`, `archive`).
- **Fixes:**
  - retry without TLS verification when verification fails
  - NS: retry with fewer parallel queries and an optional resolver, falling back to registry NS
  - TRABIS `.tr` WHOIS parser
  - DOCTYPE-tolerant sitemap parsing with `defusedxml`, sitemap-index follow (caps: 10 files, 50k URLs, 500 kept)
  - Wayback timeout 6 s
- **Additions:** contacts and socials, favicon, timing and size, compression, redirect checks, ASN/org, reverse DNS, email provider, WordPress theme and plugin hints, headings, hreflang, OG/Twitter, link counts, consent tool, bot-protection detection.
- **SSRF guard** shared with the crawler (`site_tools/netguard.py`).
- **Refactor** into a `site_profile/` package with unit-testable modules; keep `site_profile.py` as a CLI shim.

**Revisit if:** you answer Open Question 1 with "no Python". Then the profiler goes to PHP (option B), and DNS is done through DNS-over-HTTPS (Cloudflare/Google JSON API via cURL), which avoids `dns_get_record` limits.

---

## D2. Content crawler: rewrite in PHP or keep Python?

**Options:** A. keep Python and rework its output layer · B. rewrite in PHP · C. use a third-party tool (wget/HTTrack mirror plus a converter).

- **B:** the hard parts are already solved in Python: the div-soup→Markdown renderer, `srcset`/lazy-image handling, catch-all detection, WordPress REST export. PHP lacks a good HTML-to-Markdown path for messy markup (league/html-to-markdown is weaker on div-soup and would add Composer runtime deps). Crawls run for minutes, so they are background jobs regardless of language.
- **C:** mirrors give HTML, not clean Markdown + structure. Adding another binary to install on Windows is no better than Python.

**Choice: A.** Keep the extraction core (`render.py`, `images.py` URL logic, `urls.py`, `robots.py`, `wordpress.py`). Rebuild the **output and orchestration** layer:
- **New layout** (PRD §5.11): one folder per run, flat `pages/`, global de-duplicated `images/` and `files/`, plus `manifest.json`, `structure.json`, `assets.json`, `urls.json` and `README.md`.
- **Output writers:** an incremental atomic manifest writer; relative link rewriting; asset store keyed by SHA-256 with readable names; image dimensions via a tiny header sniff (PNG/JPEG/GIF/WebP), so no Pillow.
- **Limits:**
  - `max_pages` applies to WordPress too; WordPress lists are fetched **lazily per page of results**, and the media library only on demand
  - the sitemap is streamed with a cap
  - total and per-file size caps; file-type filters
- **Runtime:** concurrency 1–4 (httpx client pool) with a global rate limiter; a cancel file plus SIGTERM handling that flushes the manifest; the shared SSRF guard on every request and redirect.
- **Contract:** the NDJSON events and exit codes from D1, and `--spec` input.
- **Tests:** pytest on fixture HTML (renderer regressions), slugging, link rewriting, dedupe and the manifest.

JavaScript-rendered sites (e.g. Wix) stay a known limit. An optional Playwright renderer is in "later" (F31), behind a flag, so the base install stays light.

---

## D3. Profile storage

**Options**

| | A. JSON files on disk only | B. SQLite JSON column only | C. Normalised tables only | **D. Hybrid** |
| --- | --- | --- | --- | --- |
| Fast list filtering | ✗ (scan files) | ~ (`json_extract` in WHERE, no index) | ✓ | ✓ |
| Keeps everything the script emits | ✓ | ✓ | ✗ (schema churn on every new field) | ✓ |
| History and diff | manual | ✓ | hard | ✓ |
| Backup = one file | ✗ | ✓ | ✓ | ✓ (crawl files aside) |

**Choice: D (hybrid).**
- `profile_snapshots.raw_json` keeps the exact script output (5–15 KB per snapshot), so nothing is lost and future normalisers can reprocess old snapshots.
- `profile_snapshots.summary_json` holds the normalised, version-independent object the UI and diff use (PRD §7.3).
- The **filterable facts of the latest snapshot are copied to real, indexed columns on `sites`**: `cms`, `server`, `hosting`, `http_status`, `last_fetched_at`, `last_fetch_outcome`, `tls_valid_until`, `domain_expires_on`, `registrar`, `ip_address`.
- `snapshot_technologies` rows make "has technology X" filters fast.
- **Crawl output** (images, files: MB to GB) lives **on disk** under `data/crawls/`, with only an index row in `crawls`. Large binaries in SQLite would make backups slow.

Size check: 1,000 sites × 10 snapshots × 15 KB ≈ 150 MB worst case. SQLite handles that easily, and a "keep last N snapshots per site" setting (default unlimited) is available.

---

## D4. Job runner (Windows and Linux)

**Options**
- **A. Synchronous in the request** with `set_time_limit(0)`. ✗ Nginx's `fastcgi_read_timeout` still cuts it off, it blocks a php-cgi worker, and it offers no progress or cancel.
- **B. Spawn Python directly from the request**, with Python writing to SQLite itself. ~ This duplicates DB logic in Python and splits ownership of the schema. Cancellation and orphan detection are also harder.
- **C. Detached PHP CLI worker spawned on demand.** It reads the `jobs` table, runs the Python children, owns all DB writes, and exits when idle. A **Task Scheduler daemon** mode is the fallback.
- **D. Permanent service** (NSSM, Windows service, supervisord). ✗ Too much setup for a personal tool, and it's always running.
- **E. External queue** (Redis and the like). ✗ Overkill.

**Choice: C.**
- **One owner of the DB:** PHP, which is also where migrations and validation live. Python stays DB-agnostic: it reads a spec, emits events and writes files. That keeps the scripts usable from the CLI.
- **Self-healing:** any API call that sees queued jobs and no live worker (`flock` on `data/run/worker.lock`) spawns one, so nothing needs to be "started".
- **Nothing runs when idle.**
- **Per-OS mechanics:**
  - Windows: `start "" /B` via `popen` for spawning, `taskkill /T /F` for the process tree.
  - Linux: `setsid nohup … &` and process-group signals.
  - Details in PRD §9.
- **Risk:** Appytizer may kill php-cgi children (Windows Job Objects). The **M0 spike** tests this. The fallback is the same `worker.php --daemon`, registered in Task Scheduler "at log on". The Health page detects which mode is active.

---

## D5. UI stack and libraries

The existing setup is kept: React 19, Vite 8, TypeScript 6, Tailwind 4, shadcn `base-nova` (Base UI primitives), lucide icons, TanStack Query, the Inter font and the dark ThemeProvider. It is modern, already configured, and what the boilerplate was built for.

| Need | Options considered | Choice | Why |
| --- | --- | --- | --- |
| Routing + URL-state filters | React Router 7, TanStack Router, wouter | **@tanstack/react-router** | Type-safe, validated **search params** are first-class, which is exactly "filters in the URL". It pairs with TanStack Query loaders. Code-based routes, no codegen plugin needed. |
| Data table | AG Grid, MUI DataGrid, TanStack Table | **@tanstack/react-table** | Headless, so it fits shadcn styling. Server-side sort and pagination plus row selection for bulk actions. Small. |
| Server state | — | TanStack Query (already present) | Polling for jobs (`refetchInterval`), optimistic updates for status, tags and to-dos. |
| Validation (client) | yup, valibot, zod | **zod** | Search-param schemas for the router, form validation, API response guards. |
| Forms | react-hook-form, plain | Plain controlled components + zod | Forms are small; avoids a dependency. |
| Toasts | — | **sonner** (shadcn's default) | Job completion notifications. |
| Dates | dayjs, date-fns, Temporal | **date-fns** | Tree-shakable; relative time, ranges. |
| Command palette (v1.1) | kbar, cmdk | **cmdk** (shadcn `Command`) | Already the shadcn standard. |
| Drag & drop (to-do reorder, v1.1 Kanban) | dnd-kit, pragmatic-dnd | **@dnd-kit/core** | Accessible, small. |
| JSON tree / diff | react-json-view (unmaintained), jsondiffpatch | **Own small components** | Text-only rendering (XSS-safe). The semantic diff comes from the server. |
| Charts | — | none in MVP | Not needed yet. |

Note: `cn` (imported by all shadcn components) is shadcn's own `clsx` + `tailwind-merge` replacement package, so it stays as is. `@tanstack/react-table` is not installed yet: the preview uses plain shadcn `<Table>`, and it is added in M3 when server-side sorting and selection arrive.

---

## D6. Markdown rendering and editor

**Rendering options:** `marked` + DOMPurify (renders an HTML string, needs `dangerouslySetInnerHTML`) · `markdown-it` + DOMPurify (same) · **`react-markdown` + `remark-gfm` + `rehype-sanitize`** (builds a React element tree, never an HTML string).

**Choice: react-markdown.** It is **safe by construction**: raw HTML is off by default, `rehype-sanitize` applies a GitHub-like allow-list on top, and URLs are filtered (`javascript:` dropped). GFM covers tables, task lists, strikethrough and autolinks. Custom renderers give links `target=_blank rel=noopener`, code blocks a copy button, and crawl image links rewritten to the file endpoint. The same component renders notes and crawled pages.

**Editor options:** plain textarea · CodeMirror 6 (`@uiw/react-codemirror`, ~150 KB) · Milkdown/TipTap WYSIWYG (heavy, round-trip issues).

**Choice: a textarea with split preview in the MVP.** It gets small helpers: Tab indent, `Ctrl+B`/`Ctrl+I`, list continuation on Enter, and `Ctrl+S`. CodeMirror is a drop-in upgrade later (lazy-loaded) if syntax highlighting is wanted. No WYSIWYG: notes stay portable Markdown.

---

## D7. Credentials / access handling

**Options**
- **A. Plain-text passwords in SQLite.** ✗ The DB file gets backed up, copied and maybe synced, and anyone or anything with file access gets every client's credentials.
- **B. Encrypt locally** (libsodium `secretbox`, key from a master passphrase via Argon2id; unlock per session). ~ Doable, but key management in a stateless PHP app is awkward: the passphrase must be re-entered or the key cached somewhere. You'd be building a weaker password manager.
- **C. References only.** Track *what* access exists and its state (`needed`, `requested`, `received`, `n/a`), the username if harmless, and **where the secret lives** ("Bitwarden → Clients/Fora/cPanel", a URL to the vault item).
- **D. Integrate a password manager CLI** (Bitwarden `bw`, 1Password `op`). ✗ Brittle, and per-tool.

**Choice: C for v1.1 (F22), with B as an optional "later" feature (F32)** if you really want secrets in the app. In that case it would use per-item encryption with `sodium_crypto_secretbox`, a key derived from a passphrase (Argon2id, stored salt), the key kept only in `sessionStorage` for the tab session, the secret sent encrypted, and no recovery. Recommendation: keep secrets in a real password manager and use this app as the **checklist plus pointer**, which is what a freelancer needs day to day ("do I have DNS access yet?").

---

## D8. Backend structure

**Options:** Slim / Laravel / Symfony micro · a light Composer router (FastRoute) · **hand-rolled**.

**Choice: hand-rolled, no runtime Composer dependencies.**
- a ~150-line `Router` (method + path patterns like `/api/sites/{id:\d+}`)
- `Request` and `Response` helpers
- a PSR-4 autoloader in `bootstrap.php`
- controllers, plus small domain services using PDO directly

Why:
- (1) `pnpm build` copies `backend/` and must work without running `composer install` on Windows.
- (2) The API is CRUD plus a job queue, and a framework adds nothing that pays off here.
- (3) It is easy for you and your agents to read.

Composer is used **only in development** for PHPUnit (`backend/composer.json` with `require-dev`), and `vendor/` is excluded from the build copy. Requires PHP ≥ 8.1 (enums, readonly properties, `never`). The current container has 8.3. The Windows version is in Open Question 2.

---

## D9. URL layout and SPA routing under Appytizer

**Problem:** the SPA needs deep links (`/sites/12`) to load `index.html`, and the API needs a stable entry point. Appytizer's Nginx rules are unknown.

**Options:**
- A. `dist/index.php` front controller (Valet-style servers send non-file paths to `index.php`)
- B. Hash routing (`/#/sites/12`) plus `/backend/index.php?r=/sites/12`, which works on any server
- C. Ship an Nginx config snippet

**Choice: A, with B as a one-flag fallback.**
- `public/index.php` is copied by Vite to `dist/index.php`. It routes `/api/*` to `backend/index.php` and serves `index.html` for everything else (with `no-cache`), while static assets are served directly by Nginx.
- The API client builds URLs in one place (`apiUrl()`), and the router can be created with hash history from one config flag (`VITE_ROUTER_MODE=hash`).
- In dev, Vite handles history fallback and proxies `/api` to `php -S`.
- The M0 spike verifies A on Appytizer (Open Question 2a).

---

## D10. Data location and build safety

**Problem:** today the DB can end up inside `dist/` (junction path detection), where the next build deletes it.

**Choice:**
1. Data lives in **`<project_root>/data`** (gitignored) unless `APP_DATA_DIR` or `config.local.php` overrides it.
2. PHP running from `dist/` finds `project_root` through **`backend/build-info.php`**, generated at build time with the absolute `realpath` of the repo. Junction-resolved `__DIR__` values therefore no longer matter.
3. The Vite build plugin **asserts the data dir is not inside `dist/`** and never copies `data/`.
4. At startup, `Config` refuses to open a DB under the web-served directory, with a clear error in the API and on the Health page. It also offers a one-time move of legacy `backend/data/app.sqlite`.
5. Automatic backup before every migration; manual backup button; a retention policy.

Rejected: putting data in `%LOCALAPPDATA%`. It's invisible to you and harder to back up or open, and php-cgi's environment may not have the variable. You can still choose it via config (Open Question 3).

---

## D11. Python environment management

**Options:** system Python + `pip install --user` · `venv` + `requirements.txt` · **uv project** · PyInstaller-frozen `.exe`.

**Choice: uv, as a single project for both scripts** (confirmed). One `scripts/pyproject.toml` covers the profiler and the crawler, with the union of their dependencies, one `uv.lock` and one `scripts/.venv`; shared code lives in `scripts/site_tools/`.
- `scripts/pyproject.toml` + `uv.lock` give reproducible pins.
- `uv sync --project scripts` creates `scripts/.venv`, and PHP calls that interpreter **by absolute path**, so it doesn't depend on PATH.
- The crawler README already assumes uv.
- `uv` can also install Python itself (`uv python install 3.12`) if none is present on Windows.

A frozen `.exe` (PyInstaller) is the escape hatch if you want zero Python on the machine: one build step on Windows produces `site_profile.exe` and `crawler.exe`, and Config takes an `APP_PROFILER_BIN` override. It is not planned for the MVP.

---

## D12. CSRF and host safety model

The app has no login, so classic session CSRF tokens protect nothing. The real risks are **other websites in the same browser** calling `http://dashboard.test/api/...`, and **DNS rebinding**.

**Choice:**
- **Mutations:**
  - must be `Content-Type: application/json`, which an HTML form cannot send
  - must carry the header `X-Requested-With: freelance-dashboard`; a custom header forces a CORS preflight, and the API never answers preflights with allow headers, so cross-site requests are blocked by the browser
  - if an `Origin` header is present, it must be allowed
- **All requests:** the `Host` must be in `allowed_hosts`, otherwise `421 Misdirected Request` (stops DNS rebinding).
- **GET endpoints are side-effect free.** `ensureWorker()` only spawns for jobs that already exist, so it is idempotent and harmless.

This is simpler than a token and just as effective for a cookie-less local API.

---

## D13. Snapshot diff

**Options:** raw JSON diff on the client (jsondiffpatch) · **server-side semantic diff over the normalised summary**.

**Choice: semantic diff in PHP (`SnapshotDiff`).**
- Raw diffs are noisy (header timestamps, `cf-ray`, `age`, ordering, timings) and depend on the schema version.
- Diffing `summary_json` gives human-labelled changes grouped by section: set semantics for lists, scalar changes for values, and an ignore-list for noise.
- It also lets ingest store `changes_count` for the history list and the "3 changes" toast.
- A raw line diff (jsondiff on pretty-printed JSON, computed client-side, lazy-loaded) is available behind "Show raw diff" for anything the normaliser doesn't cover.

---

## D14. Testing tools

| Layer | Tool | Scope |
| --- | --- | --- |
| Python | **pytest** (dev dependency in `pyproject.toml`) | Parsers (RDAP, WHOIS incl. TRABIS, sitemap, contacts, tech detection), netguard, crawler slug/link-rewrite/dedupe/manifest. Offline fixtures only: saved HTML, WHOIS and sitemap bodies, plus the 4 real profile JSONs as v1 golden files. |
| PHP | **PHPUnit 11** (dev-only Composer) | UrlNormalizer + PSL, SiteQuery, ProfileNormalizer v1/v2, SnapshotDiff, SafePath, Guard, Migrator. **API integration:** boots `php -S` on a temp data dir and exercises the endpoints. **Job runner:** a fake Python script emitting scripted events (success, partial, crash, slow for cancel and timeout, orphan). |
| Frontend | **Vitest** + Testing Library (jsdom) | Search-param schemas (URL ⇄ filters round-trip), the Markdown sanitisation snapshot (XSS payloads), diff rendering, job progress component. |
| Manual | Checklist in `IMPLEMENTATION_PLAN.md` | Windows/Appytizer verification per milestone (spawn, cancel, junction build, open folder). |

`pnpm test` runs all three. Each milestone ends green on `pnpm lint && pnpm typecheck && pnpm test && pnpm build`.
