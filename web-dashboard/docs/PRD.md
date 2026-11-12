# Freelancer Dashboard: Product Requirements & Technical Design

| | |
| --- | --- |
| Status | **Draft for review.** No app code until this is approved. |
| Date | 2026-10-26 |
| Companion docs | [`DECISIONS.md`](./DECISIONS.md) (every "you decide" item), [`IMPLEMENTATION_PLAN.md`](./IMPLEMENTATION_PLAN.md) (milestones and checklists) |
| Source brief | [`prompt.md`](./prompt.md) |

---

## 1. Findings: current repository and scripts

I explored the repo and ran both scripts in the Linux dev container: `site_profile.py` against 4 URLs (a WordPress site, `example.com`, a self-signed TLS site, a non-existent domain), and `generic_crawler` against a static scraping sandbox (8 pages) and a large WordPress site. I also read the 4 real profile JSONs in `scripts/generic_crawler/examples/`.

### 1.1 Repository boilerplate

| Area | What exists | Notes |
| --- | --- | --- |
| Frontend | Vite 8, React 19, TS 6, Tailwind 4, shadcn (`base-nova` style on `@base-ui/react`), lucide, TanStack Query, a dark theme provider | `App.tsx` is a 543-line notes-board demo. We replace it. The theme, button and Tailwind tokens can stay. |
| Backend | `backend/api.php`, one file: GET and POST on a `notes` table, PDO SQLite, `CREATE TABLE IF NOT EXISTS` | No router, migrations or CSRF checks. |
| Build | A Vite plugin copies **only** `backend/api.php` to `dist/backend/` | It needs to copy the whole backend except data and tests. |
| Dev proxy | `/backend` is proxied to `https://dashboard.test` (`secure:false`) | Appytizer serves `http://`. On Linux there is no `dashboard.test`. |
| **Data safety risk** | `api.php` picks the data dir with `basename(dirname(__DIR__)) === 'dist' ? project root : dirname(__DIR__)`. | `dist/` is served through the junction `C:\appytizer\dashboard`, so `__DIR__` can resolve to `C:\appytizer\dashboard\backend`. The check then fails and the DB is created in **`dist/backend/data`**. Vite's `emptyOutDir` then **wipes it on the next `pnpm build`**. The new path strategy in §10 fixes this by design. |
| Missing | No `pyproject.toml` or `requirements.txt` for either script, though the crawler README says to run `uv sync`. `examples/fora.toml` is referenced but missing. `examples/*.json` hold **profiler** outputs, not crawler examples. | |

### 1.2 `scripts/site_profile.py` (profiler)

**What it does:** a passive profile built from one HTTP GET of the given URL, DNS (A, AAAA, CNAME, NS, MX, TXT, CAA, SOA, DNSKEY, plus `_dmarc`), the TLS certificate, RDAP with a WHOIS fallback (hard-coded for `.tr`, `.com`, `.net`, `.org`), robots.txt plus up to 3 sitemaps, the Wayback CDX first and last capture, and heuristic technology detection with evidence and confidence.
Dependencies: `dnspython`, `tldextract` (bundled PSL snapshot, works offline) and `beautifulsoup4`. Python 3.10+.

**Output shape** (top-level keys, stable across the 4 samples):
`metadata`, `input`, `domain_registration`, `website_history`, `http` (status, final_url, redirects[], notable_headers, security_headers{present,missing}, cookies[]), `page` (title/description with lengths, generator, lang, canonical, robots, OG title/image, h1/image/form/script counts, images_missing_alt, external_script_hosts, structured_data_types), `dns` (`domain_records`, `website_host_records`, each holding `records`, `spf_records`, `dmarc_records`, `dnskey_present` and `errors`), `tls_certificate`, `robots_and_sitemaps`, `technologies[] {category,name,confidence,evidence[]}`, `limitations[]`.

**Runtime:** 11–38 s per site. Most of it is WHOIS, Wayback and timeouts on dead hosts. This rules out a synchronous call inside a php-cgi request.

**Weaknesses and failure modes I observed:**

1. **Not machine-friendly.**
   - With `--json`, stdout starts with `JSON report saved to …`, so stdout is not valid JSON.
   - It **always** writes a file, by default to the hard-coded Windows path `D:\Projects\Freelance\!Reports`.
   - The exit code is `0` even when the site is unreachable (NXDOMAIN and the TLS-failure tests both returned 0).
   - There is no progress output.
2. **Unstable schema on failure.** `page` and `tls_certificate` change shape when they fail (`{parseable_html:false, error}` instead of the full key set). There is no overall `status` and no structured error list, only free-text `error` strings in many places.
3. **One TLS failure loses everything.** For the self-signed site (`resoltenerji.com`, and badssl in my test), the whole HTTP section is empty: no page metadata, technologies or robots/sitemaps. The profiler should retry without verification, collect the data, and flag the certificate as invalid.
4. **NS lookups time out in all 4 of your real samples**: `The resolution lifetime expired after 5.1 s` against your router `192.168.1.1`. NS is therefore always empty. The likely cause is 9 parallel queries, one with EDNS DO for DNSKEY, flooding a home router. The fix is retries, fewer parallel queries, an optional public resolver, and a fallback to registry nameservers.
5. **`.tr` WHOIS is only partly parsed.** The `forazeytin.com.tr` sample has no registrar, expiration date or nameservers, and `last_updated` is the WHOIS database timestamp. TRABIS uses `Expires on.....:` and `** Registrar:` / `** Domain Servers:` blocks that the parser does not handle.
6. **Sitemap parsing is too strict.** A sitemap with a DOCTYPE is rejected (`forazeytin`). Sitemap indexes are not followed, so you never see a total URL count. Only 5 sample URLs are kept, which is not enough for a "pages" view.
7. **Wayback lookups time out often** (2 of 4 samples). That's fine, but they should be a non-blocking section with a short timeout.
8. **Missing information** that a freelancer needs (§5.4):
   - contact data (emails, phones, address) and social links
   - favicon; response time and page weight; compression; HTTP→HTTPS and www canonicalisation checks
   - IP → hosting provider (reverse DNS, ASN or organisation)
   - email provider (from MX)
   - WordPress theme, plugins and version; PHP version (only via `X-Powered-By` today)
   - heading outline; hreflang; Twitter card and the full OG set
   - internal and external link counts; cookie-consent tool; screenshot
9. **Heuristic false positives** seen in the samples: Fastly (from `x-served-by: cache-…`) and React on a Wix site. That's acceptable if the UI shows confidence and evidence.

### 1.3 `scripts/generic_crawler/` (content download)

**What it does:** a BFS crawl from a start URL, plus `sitemap.xml` and the sitemaps listed in robots.txt. It respects robots.txt and Crawl-delay, removes header, footer and nav, converts HTML to Markdown (handling div-soup, tables, lists, lazy images and the largest `srcset` candidate), and downloads images. Linked documents are downloaded with `--download-docs`. WordPress sites are exported through the REST API. It writes a `README.md` index at the end.
Dependencies: `httpx`, `beautifulsoup4`, `lxml`. Python 3.11+ (`tomllib`).
Measured: about 4.4 s per page at the default 0.2 s delay, with images.

**Why the output is chaotic:**

1. **Redundant, long, inconsistent paths.** Example: `en/catalogue__category__books__travel_2__index/en__catalogue__category__books__travel_2__index.md`. Language and slug are repeated in both the folder and the file name. WordPress items use a second scheme (`wordpress/<slug>/wordpress__<slug>.md`, with `page-` prefixes), so one site's pages sit in two unrelated trees.
2. **Images are copied once per page.** The same file sits in many page folders: 141 files, 117 unique, 17 % duplicates on an 8-page test. There is no global asset list, no image→page mapping and no metadata (alt, dimensions, size).
3. **Nothing is machine-readable.** There is only a Markdown README. No JSON manifest, page tree, headings outline or link graph exists, so an AI agent has to re-parse everything.
4. **Internal links in the Markdown point to the live site**, not to the exported `.md` files.
5. **The index is written only at the end.** In my test, a crawl killed by a timeout left **165 files and no index**. Cancel and crash both leave unusable output.
6. **`--max-pages` does not limit WordPress.** On a WordPress site with `--max-pages 3` it loaded the media library, **1,114 posts and pages** and a **16,076-URL sitemap** before crawling, then ran past my 120 s timeout. There are also no total-size or per-file-size caps, so the disk can fill up.
7. **Reruns mix.** It reuses files left in the same output folder from earlier runs, and runs are not isolated from each other.
8. **No structured progress, no cancel hook, exit codes only `0` or `1`.** Config errors raise `SystemExit("message")`.
9. **Limited file types** (images plus a fixed document list), not configurable. No SSRF guard: redirects can go to private IPs.
10. **JavaScript-rendered sites export almost no text.** One of your 4 sample sites is Wix, which is React-rendered, so it would come out nearly empty. A headless browser is needed (later).

**What works well and should be kept:**
- the HTML→Markdown renderer
- `srcset` and lazy-image handling
- detection of catch-all servers that return the home page for unknown URLs
- stripping of tracking parameters
- WordPress REST API discovery
- robots.txt handling

---

## 2. Goals and non-goals

### Goals
1. One place to manage every client website or project: status, category, tags, notes, to-dos, contacts and links.
2. A one-click **site profile** that is scannable in 10 seconds and holds the detail when needed. Re-fetching keeps a **history**, with **what changed** between fetches.
3. A **content export** that I (and my coding agent) can use directly: a predictable layout, clean Markdown, de-duplicated assets and machine-readable structure.
4. Runs reliably on **Windows / Appytizer / php-cgi** and in a **Linux dev container** from the same code.
5. **Never lose data:** builds never touch the DB or downloads, migrations take automatic backups, and a backup button exists.

### Non-goals
- Multi-user, authentication, roles, or hosting the app on a server.
- Full accounting or invoicing (generating legally valid invoices, taxes). v1.1 tracks amounts and status only.
- Active security scanning, port scanning, or anything intrusive. The profiler stays passive.
- Rendering JavaScript-heavy sites in the MVP (headless browser is "later").
- Cloud sync and mobile apps. The layout is responsive, but the app is used on the local PC.
- Being a password manager (see §5.5 and D7).

### Success criteria
- Adding a site and seeing its profile takes **under 1 minute** end to end with no terminal, and the list never freezes.
- Finding "all WordPress sites in status `waiting` tagged `seo`" takes **one URL**, which can be bookmarked.
- A crawl of a 100-page site produces an output an agent can consume from `README.md` + `structure.json` alone.
- `pnpm build` run 100 times leaves `data/` byte-identical.

---

## 3. Users and user stories

There is one user: **me**, a freelance web developer working on Windows. A secondary consumer is **my coding agent**, which reads the crawl output.

| ID | As a… | I want to… | so that… | Priority |
| --- | --- | --- | --- | --- |
| US-01 | freelancer | paste a URL (with optional category and status) and get a profile | I know the client's current stack before the first call | MVP |
| US-02 | freelancer | be warned when the domain already exists | I don't create duplicates | MVP |
| US-03 | freelancer | see the profile grouped into readable sections with copy buttons | I can answer "where is it hosted / which CMS" instantly | MVP |
| US-04 | freelancer | see a clear partial or failed state with the reason | I trust what's shown and know what's missing | MVP |
| US-05 | freelancer | re-fetch and compare with the previous snapshot | I see what the client or old agency changed | MVP |
| US-06 | freelancer | filter and sort the list by status, category, tags, CMS, date and text, and bookmark it | I find the right projects fast | MVP |
| US-07 | freelancer | save filter views and run bulk actions | I can triage many sites at once | MVP |
| US-08 | freelancer | keep several Markdown notes per project with preview | meeting notes and briefs live with the project | MVP |
| US-09 | freelancer | manage to-dos with due date and priority, and see all open to-dos | nothing slips across projects | MVP |
| US-10 | freelancer | see an activity log per project | I remember what happened when | MVP |
| US-11 | freelancer | start a content download with options and watch progress or cancel | I get the copy and images for a rebuild without a terminal | MVP |
| US-12 | freelancer | browse pages, images and files from a crawl, download a ZIP or open the folder | I can use the content right away | MVP |
| US-13 | coding agent | read `README.md`, `structure.json` and `pages/*.md` from a crawl | I understand the site without crawling it again | MVP |
| US-14 | freelancer | see a home dashboard (active projects, deadlines, open to-dos, expiring SSL/domains, running jobs) | I can plan my day | MVP |
| US-15 | freelancer | store client contacts and project links (prod, staging, repo, hosting, registrar) | everything is one click away | MVP |
| US-16 | freelancer | back up the database with one click | I can experiment safely | MVP |
| US-17 | freelancer | track which accesses I have (FTP, hosting panel, DNS…) without storing passwords | I know what to ask the client for | v1.1 |
| US-18 | freelancer | track offer amount, invoice and payment status | I know who still owes me | v1.1 |
| US-19 | freelancer | see a Kanban board by status | I can move projects visually | v1.1 |
| US-20 | freelancer | search globally across sites, notes and to-dos (⌘K) | I find anything instantly | v1.1 |
| US-21 | freelancer | get reminders for domain and SSL expiry and deadlines | I can renew on time | v1.1 |
| US-22 | freelancer | apply to-do and note templates per project type | onboarding a project is fast | v1.1 |
| US-23 | freelancer | attach files (briefs, contracts, screenshots) | documents live with the project | v1.1 |
| US-24 | freelancer | have profiles re-fetched on a schedule and uptime checked | I notice outages and changes | later |
| US-25 | freelancer | run a Lighthouse, broken-link, SEO or accessibility audit | I can quote improvements with evidence | later |
| US-26 | freelancer | crawl JavaScript-rendered sites and capture screenshots | Wix, React and other SPA sites work too | later |

---

## 4. Feature list and priorities

**MVP**: what I need to stop using the CLI scripts. **v1.1**: high value and cheap once MVP exists. **Later**: needs a heavier dependency or a scheduler.

| # | Feature | MVP | v1.1 | Later |
| --- | --- | :-: | :-: | :-: |
| F1 | Add site: URL normalisation, duplicate warning, optional category, status and tags, "fetch now" | ● | | |
| F2 | Profile fetch as a background job, with progress and graceful partial or failed states | ● | | |
| F3 | Profile detail view: sectioned cards, badges, copy buttons, raw JSON tab | ● | | |
| F4 | Snapshot history and diff ("what changed") | ● | | |
| F5 | Site list: table view, filters (status, category, tags, CMS, technology, fetch date, text), sorting, URL query state, facet counts | ● | | |
| F6 | Saved views and bulk actions (status, category, add/remove tag, archive, re-fetch) | ● | | |
| F7 | Card layout toggle for the list | | ● | |
| F8 | Notes: multiple per site, Markdown with split live preview, sanitized rendering, pin | ● | | |
| F9 | To-dos: per site, open/done, due date, priority, reorder; global "open to-dos" page | ● | | |
| F10 | Activity log (status changes, fetches, notes, to-dos, crawls) | ● | | |
| F11 | Content download job: options, progress, logs, cancel | ● | | |
| F12 | Crawl result browser: pages (rendered Markdown), image gallery, files, structure tree; ZIP; open folder | ● | | |
| F13 | Job center (all running and finished jobs, logs, retry) | ● | | |
| F14 | Dashboard home: active by status, upcoming deadlines, open to-dos, SSL and domain expiring ≤ 30 days, recent fetches, running jobs | ● | | |
| F15 | Client contacts and project links per site | ● | | |
| F16 | Settings and health check (paths, Python, PHP CLI, data dir, extensions) | ● | | |
| F17 | One-click SQLite backup (`VACUUM INTO`) and automatic backup before migrations | ● | | |
| F18 | Dark theme (default), light/system toggle, responsive layout, empty, loading and error states | ● | | |
| F19 | Keyboard shortcuts (`/` focuses search, `n` adds a site, `g s` goes to sites) and a command palette (⌘K) | | ● | |
| F20 | Global search (FTS5 over sites, notes, to-dos) | | ● | |
| F21 | Kanban board by status (drag to change status) | | ● | |
| F22 | Access checklist (references only, no secrets), see D7 | | ● | |
| F23 | Offer, invoice and payment tracking per project, plus an "unpaid" widget | | ● | |
| F24 | Milestones and timeline per project | | ● | |
| F25 | Expiry and deadline reminders (in-app banner and dashboard; optional Windows toast via a scheduled task) | | ● | |
| F26 | Templates: to-do checklists and note templates per project type | | ● | |
| F27 | Attachments (upload, preview images and PDFs) | | ● | |
| F28 | Export JSON/Markdown per project, import from JSON, import existing `!Reports/*.json` profiles | | ● | |
| F29 | Scheduled re-fetch and uptime/health check (Windows Task Scheduler running a CLI) | | | ● |
| F30 | PageSpeed/Lighthouse (PSI API), broken-link checker, SEO/accessibility quick audit | | | ● |
| F31 | Headless rendering (Playwright, optional) for JS sites and screenshots during fetch | | | ● |
| F32 | Optional encrypted secrets vault (libsodium, master passphrase), see D7 | | | ● |

---

## 5. Feature specifications

### 5.1 Add site and URL normalisation (F1)

- Input accepts `example.com`, `www.example.com/path`, `https://…`, and IDN (`çiçek.com.tr`).
- Normalisation happens server-side in PHP, in `UrlNormalizer`, using the same rules as Python:
  1. trim, and add `https://` when the scheme is missing; only `http` and `https` are allowed, and userinfo is rejected
  2. lowercase the host, convert IDN to punycode (`idn_to_ascii`), remove the trailing dot
  3. **site key** = host without a leading `www.`; **registrable domain** = eTLD+1 from a bundled Public Suffix List (`backend/resources/public_suffix_list.dat`)
- Duplicate detection runs live while typing, debounced through `GET /api/sites/check-url`:
  - the same site key exists → **blocking warning** "Already in your list → open". You can still add it via "Add anyway as separate project", which uses a disambiguated key such as `example.com#2`.
  - the same registrable domain under a different subdomain → **soft hint** "Related: shop.example.com".
- The network guard (§11) rejects private, loopback and link-local targets unless "Allow local/LAN target" is ticked, for example for a staging box.
- On submit, the site is created and, if "Fetch profile now" is ticked (the default), a `profile` job is queued and the user lands on the detail view with a progress card.

### 5.2 Profile fetch (F2)

- Runs as a `profile` job (§9). The UI shows these phases from progress events:
  `resolving → http → page → dns → tls → registration → sitemaps → archive → done`.
- Typical time is 10–40 s. The hard limit is 120 s, after which the job is killed and marked `failed: timeout`.
- **Outcomes:** `ok`, `partial` (some sections failed; the snapshot is stored and the failed sections are listed), `failed` (site unreachable, nothing useful; the snapshot is still stored with its errors so history shows the attempt). Each outcome writes an activity entry.
- **Blocked or odd sites:** TLS errors trigger a retry without verification and set the `tls.verified=false` badge. HTTP 403/503 challenge pages (Cloudflare and similar) are detected and flagged "Blocked by bot protection". A non-HTML response shows the content type.

### 5.3 Profile detail view (F3)

Data comes from the **normalised snapshot** (`summary_json`, built by PHP `ProfileNormalizer` from raw schema v1 or v2), so the UI never reads raw keys directly. The sections, in order:

| Section | Contents | UI elements |
| --- | --- | --- |
| **Status banner** | Only for partial or failed: which sections failed and why, with a "Re-fetch" button | Alert with an expandable error list |
| **Overview strip** | HTTP status, final URL (redirect count), CMS, Hosting/edge, Server, SSL expiry (days), Domain expiry, Domain age, Last fetched | 8–9 stat cards; colour by health (SSL < 14 d red, < 30 d amber) |
| **Tech stack** | Technologies grouped by category (CMS, Framework, Library, Analytics, Hosting/edge, Server, Language); version when known; WordPress theme and plugins | Badges with a confidence dot; popover with evidence |
| **Hosting, DNS and email** | IPs plus hosting org (ASN), reverse DNS; NS (with registry fallback); MX plus detected email provider; SPF / DMARC / DNSSEC / CAA as pass/missing badges; TXT records collapsed | Key-value cards, small tables, copy buttons |
| **SSL / TLS** | Verified?, issuer, valid from/until, days left, SAN count plus a sample, fingerprint, cipher | Card with an expiry progress bar |
| **Domain registration** | Registrant (the owner named in the registry, when not hidden), registrar, created, expires, updated, statuses, **transfer lock (locked / unlocked / unknown)**, lookup method and source | Card; a lock badge so a needed transfer is not a surprise |
| **SEO and meta** | Title (length gauge 30–60), description (gauge 70–160), canonical, meta robots (noindex highlighted), lang, hreflang list, OG/Twitter preview card with image, structured-data types, H1 count, heading outline, images missing alt | Gauges, preview card, collapsible outline |
| **Performance** (lightweight) | Response time (TTFB, total), HTML size, compression, redirect chain, cache headers, HTTP→HTTPS and www→apex checks | Stat cards plus a redirect-chain stepper |
| **Pages and sitemap** | robots.txt status and rules count, sitemap(s) with total URL count (following the index), URL list (up to 500, filterable, linked) | Table with a search box; "Use as crawl seed" action |
| **Contact and social** | Emails, phone numbers (from `mailto:`, `tel:`, schema.org, footer text), address if present, social profile links, WhatsApp | List with copy and open buttons and network icons |
| **Security headers and cookies** | Present/missing checklist (HSTS, CSP, XFO, XCTO, Referrer-Policy, Permissions-Policy); cookies with Secure, HttpOnly and SameSite flags | Checklist, table |
| **Web history** | Earliest and latest Wayback capture with links | Small card |
| **Limitations** | The script's `limitations[]` | Muted footnote |
| **Raw JSON** (tab) | Full raw snapshot | Collapsible JSON tree, search, "Copy JSON", "Download .json" |

Every value that might be pasted somewhere (IP, NS, MX, emails, fingerprints, URLs) has a copy button. External links open in a new tab with `rel="noopener noreferrer"`.

### 5.4 Profiler changes needed (Python v2)

Details are in D1. Summary:
- **CLI contract:**
  - `site_profile.py URL --output FILE --events`
  - stdout carries only NDJSON events; the JSON report goes to `--output` (or to stdout as the single document with `--json` and no `--events`)
  - no hard-coded paths
- **Schema v2:**
  - `schema_version: 2`, `status: ok|partial|failed`
  - `errors: [{section, code, message}]`, `timings_ms` per section
  - every section always has the same keys, with `null` when unknown
- **Exit codes:** `0` ok · `1` partial · `2` invalid input · `3` unreachable / failed · `4` blocked by network policy · `130` cancelled · other = crash.
- **Fixes:** TLS-failure fallback, NS retry and fallback, TRABIS WHOIS parser, tolerant sitemap parsing (DOCTYPE stripped, `defusedxml`), sitemap index follow with caps, shorter Wayback timeout.
- **New fields:**
  - contacts and socials; favicon
  - timing and size; compression; HTTP→HTTPS and www checks
  - ASN/org via IP RDAP; reverse DNS; email provider from MX
  - WordPress theme, plugins and version hints
  - heading outline; hreflang; OG/Twitter set; link counts; consent tool; bot-protection detection
- **SSRF guard:** resolve the host and refuse private ranges unless `--allow-private` is passed; check again on every redirect.

### 5.5 Website list view (F5–F7)

- **Columns:** checkbox, favicon plus **Domain** (with name below), **Backend/CMS** (badge), **Status** (coloured pill, editable inline), **Category**, **Tags** (chips, max 3 plus "+n"), **Fetched** (relative date; red dot if the last fetch failed), **SSL**, **Open to-dos** count, **Deadline**. Columns are toggleable; the choice is stored in localStorage.
- **Filters** (all serialised to the URL query string, e.g. `/sites?status=design,waiting&cms=WordPress&tag=seo&q=zeytin&fetched=2026-01-01..2026-06-30&sort=-fetched`):
  - status (multi), category (multi), tags (multi, any or all), CMS (multi, from facets), technology (contains, e.g. `jQuery`), fetch-date range, fetch result (ok/partial/failed/never), has open to-dos, deadline range, archived toggle
  - free-text search over domain, name, title, notes titles and contact name
- **Facet counts** next to each filter option, computed server-side for the current filter set.
- **Sorting:** any column, plus a secondary sort. Default `-updated`.
- **Saved views:** named filter and sort combinations in the sidebar ("Waiting on client", "WordPress rebuild leads"), saved in SQLite so they survive browser resets. Creating one from the current URL is one click.
- **Bulk actions** (a sticky bar when rows are selected): set status, set category, add or remove a tag, re-fetch profiles (queued with concurrency limit 2), archive.
- **Card layout (v1.1):** a grid of cards with favicon, title, CMS, status, tags and a mini health row. It helps when browsing visually.
- **Performance:** server-side filtering and pagination (50 per page, infinite scroll), and debounced search. Expected scale is under 1,000 sites, so SQLite with indexes is enough.

### 5.6 Website detail view

Layout: a header plus tabs, and a right-hand info sidebar (it moves below the tabs on narrow screens).
- **Header:**
  - favicon, domain (copy), name (editable), and a link to the live site
  - status select, category select, tag editor (create inline)
  - `Re-fetch profile`, `Download content`, and a `…` menu (archive, delete, export)
- **Tabs:** `Profile` (with a `Raw JSON` sub-tab) · `Notes` · `To-dos` · `Content` · `History` (snapshots and compare) · `Activity`.
- **Sidebar:** project info (deadline, created), contacts, links (prod, staging, repo, hosting, registrar, DNS), quick health (SSL and domain expiry), and v1.1 widgets (access checklist, money).

### 5.7 Notes (F8)

- A site has many notes. Each note has a title (required), a Markdown body (up to 200 KB), pinned or not, and created and updated dates.
- **Editor:**
  - `Edit | Split | Preview` modes, Split by default on wide screens
  - a textarea with a monospace font, Tab inserts spaces, `Ctrl+S` saves, autosave after 1.5 s of idle with a "Saved" indicator, and an unsaved-changes guard
- **Rendering:** `react-markdown` + `remark-gfm` (tables, task lists, strikethrough) + `rehype-sanitize`. No raw HTML passthrough. Links get `target=_blank rel=noopener`. Code blocks have a copy button.
- The list is sorted pinned first, then by updated date, with a search box.

### 5.8 To-dos (F9)

- **Fields:** title, done (with `done_at`), optional due date, priority (`low`, `normal`, `high`), optional note (one line), and a sort order (drag to reorder).
- **Per-site tab:** open items first, done items collapsed ("12 done"). Quick-add input: Enter adds the item, and `!high` or `due:fri` shortcuts are parsed.
- **Global `/todos` page:** all open to-dos grouped by `Overdue / Today / This week / Later / No date`, filterable by site, priority and status, with the site link on each item. The same component is used for the dashboard widget.

### 5.9 Activity log (F10)

The log records events automatically: `site.created`, `status.changed (from→to)`, `category.changed`, `tags.changed`, `profile.fetched (ok|partial|failed)`, `crawl.started/finished/failed/canceled`, `note.created/updated/deleted`, `todo.created/completed/reopened/deleted`, `contact/link changed`. Each entry has a type icon, a message, relative time, and a link to the related entity. The per-site timeline loads more on scroll. The global feed appears on the dashboard.

### 5.10 Snapshot history and diff (F4)

- The History tab lists the snapshots: date, outcome badge, duration, key facts (CMS, IP, SSL until) and a "changes vs previous" count.
- **Compare view:**
  - pick A and B (the default is latest vs previous)
  - shows grouped changes such as *Technologies:* `+ Cloudflare`, `− jQuery 1.12`; *DNS NS:* `ns1.old.com → ns1.new.com`; *SSL:* `valid until 2026-12-01 → 2027-03-01`; *Title:* text diff
  - unchanged sections are hidden behind a toggle
  - a "Show raw JSON diff" toggle (line diff) is the fallback
- The diff is computed server-side (`SnapshotDiff` over normalised `summary_json`): set differences for lists (technologies, IPs, NS, MX, missing security headers, structured-data types) and value changes for scalars. Noise fields such as `cf-ray`, `age`, `etag` and timings are excluded. The "changes vs previous" count is stored on the snapshot at ingest time.

### 5.11 Content download (F11, F12)

**Start crawl dialog** (from the Content tab):

| Option | Default | Notes |
| --- | --- | --- |
| Start URL(s) | the site URL; plus "use sitemap URLs" | Extra entry URLs allowed |
| Max pages / max depth | 200 / 5 | Applied to HTML **and** WordPress items |
| Scope | same host; ☐ include subdomains | |
| Include / exclude patterns | none | Regex, validated live in PHP; presets for "skip cart/search/tags" |
| Respect robots.txt | ☑ | Turning it off needs an extra confirm checkbox: "I own or may copy this site" |
| Rate limit | 0.5 s delay, 1 concurrent | Crawl-delay from robots.txt wins if higher |
| Content | ☑ images ☑ documents (pdf, doc(x), xls(x), ppt(x), csv, zip) ☐ video/audio ☐ fonts | Extension list editable |
| Size caps | max file 25 MB, max total 1 GB | The job stops gracefully at the cap |
| Extraction | main selector (auto), strip selectors, ☐ keep header/footer | Advanced section |
| Languages | auto from `<html lang>`; prefixes like `en,de` | Advanced |
| WordPress | auto / off / only | Advanced |

The options are remembered per site (`crawls.options_json` of the last crawl prefills the dialog).

**Progress panel:**
- status pill (`queued`, `running`, `canceling`, `done`, `partial`, `failed`, `canceled`)
- a progress bar (pages done of the planned total, which may grow)
- counters: pages, images, files, MB, errors; elapsed time and ETA
- the current URL, a live log tail (last 200 lines, auto-scroll, level filter) and a `Cancel` button
- progress is polled every 1 s while the tab is open and every 5 s in the job center

**Result browser** (one crawl selected; crawl list on the left with date, counts, size and status):
- **Pages:**
  - a tree by URL path, plus a searchable flat list
  - clicking a page shows its rendered Markdown, with images resolved through the file endpoint
  - shows source URL, word count, headings, and the images used on the page
  - "Copy Markdown" and "Open original"
- **Images:** a gallery grid with lazy thumbnails, filters (min size, type, page) and a lightbox with alt text, dimensions, size, "used on" pages and download.
- **Files:** a table (name, type, size, linked from) with download.
- **Structure:** a JSON tree of `structure.json` plus a link graph summary (orphans, most linked).
- **Actions:**
  - `Download ZIP`, built by an `archive` job and cached until the crawl changes
  - `Open folder`: PHP launches `explorer.exe` (Windows) or `xdg-open` (Linux); the path shown has a copy button as fallback
  - `Copy path for agent`, `Delete crawl`

**Crawl output layout (v2)**, one folder per run, so runs are never mixed:

```
data/crawls/<site-key>/<YYYYMMDD-HHMMSS>/
├── README.md          # what this is, how to read it (written for humans AND AI agents)
├── manifest.json      # run metadata: schema_version, tool version, start URL(s), options,
│                      # started/finished, status, counts, bytes, errors summary
├── structure.json     # pages[] + URL-path tree + link graph (see below)
├── assets.json        # every downloaded image/file with metadata and page usage
├── urls.json          # every URL seen: crawled | skipped(reason) | failed(reason) | external
├── pages/
│   ├── index.md                  # home page
│   ├── about-us.md
│   ├── products--olive-oil.md    # path segments joined with "--", max 100 chars + hash on overflow
│   └── en/…                      # only when the site is multilingual: pages/<lang>/…
├── images/            # global, de-duplicated by SHA-256; original file names kept, "-2" on clash
├── files/             # PDFs, Office docs, etc. (same naming rules)
└── logs/crawl.log
```

- **`pages/*.md`** have YAML front matter: `id, url, title, description, lang, source (html|wp-post|wp-page), depth, crawled_at, word_count`, plus `wp: {published, modified, author, categories, tags}` when available. Images are linked as `../images/x.jpg`. **Internal links to exported pages are rewritten** to relative `.md` paths; links to pages that were not exported stay absolute.
- **`structure.json`**:
  `{schema_version, site, generated_at, pages: [{id, url, file, title, description, lang, depth, parent_id, source, headings: [{level, text}], word_count, links_internal: [page_id|url], links_external: [url], images: [asset_id], files: [asset_id]}], tree: {…nested by path…}}`
- **`assets.json`**:
  `{assets: [{id, kind: image|file|media, url, file, sha256, bytes, mime, width, height, alt_texts: [], used_on: [page_id]}]}`
- **Incremental writes:** `manifest.json`, `structure.json`, `assets.json` and `urls.json` are rewritten atomically (temp file + rename) every 10 pages and at the end. `status` stays `running` until done, so a cancel or crash still leaves a consistent, indexed folder (`status: canceled|failed`).
- The **README.md** explains the layout, the counts and "Start with `structure.json` → `pages/`". It lists skipped and failed URLs and flags pages with no text ("likely JavaScript-rendered").

### 5.12 Dashboard home (F14)

The widgets, in a responsive grid:
1. **Active projects** by status (counts per status, clickable to the filtered list)
2. **Upcoming deadlines** (next 14 days, overdue first)
3. **Open to-dos** (overdue and this week, quick-complete checkbox)
4. **Expiring soon**: SSL ≤ 30 days, domain ≤ 60 days, from the latest snapshots
5. **Recently fetched** (last 8, with outcome badge)
6. **Running and recent jobs**
7. **Activity feed** (last 15)

v1.1 adds **Unpaid invoices** and **Reminders**.

### 5.13 Contacts and links (F15)

- **Contacts:** many per site. Fields: name, role, email, phone, preferred channel (`email`, `phone`, `whatsapp`, `telegram`, `other`), notes. Actions: `mailto:`, `tel:` and WhatsApp deep links.
- **Links:** many per site, typed `production`, `staging`, `repo`, `hosting`, `registrar`, `dns`, `analytics`, `design`, `other`, each with a label and URL. Shown as icon buttons in the sidebar.
- In v1.1, contacts can belong to a **client** entity shared by several sites. The MVP keeps contacts per site so the model stays simple; the migration to clients is planned (§7.4).

### 5.14 Settings and health (F16, F17)

- **Paths (read-only display, from config):** data dir, project root, Python interpreter, PHP CLI, uv. Each has a status: ✓ found or ✗ with a fix hint.
- **Health checks:**
  - PHP version and extensions (`pdo_sqlite`, `intl`, `zip`, `mbstring`, `fileinfo`, `openssl`)
  - SQLite version and FTS5
  - write access to the data dir
  - Python version and required packages (by running `python -m site_tools.selfcheck`)
  - a spawn test (starts a no-op job and waits for its heartbeat)
- **Taxonomy:** manage categories (name, colour, order) and tags (rename, merge, colour, delete).
- **Backups:** "Back up now", a list of backups with size and date, download, and retention of the last 20 automatic backups.
- **Appearance:** theme (dark, light or system), list density.

### 5.15 v1.1 features (short specs)

- **Access checklist (F22):** per site, items such as `Hosting panel`, `FTP/SFTP`, `WordPress admin`, `DNS`, `Registrar`, `Analytics`, `Search Console`, `Email`. Each item has a state (`needed`, `requested`, `received`, `not applicable`), a username (optional) and a **secret reference** (e.g. "Bitwarden → Clients/Fora/cPanel"). No passwords are stored (D7).
- **Money (F23):** offer amount and currency, offer date and status (`draft`, `sent`, `accepted`, `rejected`); invoices (number, amount, issued, due, status `unpaid`, `partially paid`, `paid`, `overdue`); payments (date, amount, method). The dashboard shows an "Unpaid" widget.
- **Milestones (F24):** title, due date, done. They appear on the project timeline and in dashboard deadlines.
- **Reminders (F25):** computed from SSL and domain expiry, deadlines and invoice due dates. An in-app banner and dashboard list, with snooze. Optional Windows toast through the scheduled CLI (with F29).
- **Templates (F26):** named to-do checklists and note templates, applied from the site page ("Apply template: WordPress rebuild").
- **Attachments (F27):** stored under `data/attachments/<site>/`, MIME-sniffed, max 50 MB, previews for images and PDFs.
- **Export/import (F28):** per-project Markdown bundle, full JSON export and import, and an importer for existing `!Reports/*.json` (schema v1) that creates sites and snapshots.
- **Kanban (F21), global search (F20), command palette and shortcuts (F19), card view (F7).**

---

## 6. UX: screens and key flows

### 6.1 Design principles
- Dark-first, using the existing shadcn `base-nova` tokens and Inter. Information-dense but calm: muted labels and strong values.
- One primary action per screen. Destructive actions need a confirmation and offer undo where possible: archive is undoable, delete needs a typed confirm.
- Every list and panel has **empty**, **loading** (skeletons), and **error** states (message, "Retry", "Details" toggle).
- Toasts (`sonner`) for background job completion ("Profile for fora.com.tr fetched: 3 changes").
- Responsive: the sidebar collapses to icons below 1024 px and becomes a drawer below 768 px. Tables switch to stacked rows on mobile.

### 6.2 App shell

```
┌──────────────────────────────────────────────────────────────────────────────┐
│ ◧ Freelance        [ Search sites, notes…            ⌘K ]   ⟳ 2 jobs  [+ Add site] │
├────────────┬─────────────────────────────────────────────────────────────────┤
│ ▣ Dashboard│                                                                 │
│ ◫ Sites    │                     (page content)                              │
│ ☑ To-dos   │                                                                 │
│ ⚙ Jobs     │                                                                 │
│ ─ Views ── │                                                                 │
│ • Waiting  │                                                                 │
│ • WP leads │                                                                 │
│ ─────────  │                                                                 │
│ ⚙ Settings │                                                                 │
└────────────┴─────────────────────────────────────────────────────────────────┘
```

### 6.3 Dashboard

```
┌ Active projects ───────────────┐ ┌ Upcoming deadlines ──────────┐ ┌ Expiring soon ───────────┐
│ design 4 · waiting 3 · none 12 │ │ ! renama.com.tr   overdue 2d │ │ 🔒 forazeytin  SSL  12d   │
│ done 20 · canceled 2           │ │   oleoolivia      Fri 31 Oct │ │ 🌐 example.org dom. 41d   │
└────────────────────────────────┘ └──────────────────────────────┘ └──────────────────────────┘
┌ Open to-dos (overdue · this week) ─────────────┐ ┌ Recently fetched ─────────────────────┐
│ ☐ !high  Send sitemap to client   renama  2d!  │ │ ✓ fora.com.tr     2h ago  WordPress  │
│ ☐        Ask for cPanel access    fora    Fri  │ │ ◐ resoltenerji    1d ago  partial    │
└────────────────────────────────────────────────┘ └──────────────────────────────────────┘
┌ Jobs ──────────────────────────────────────────┐ ┌ Activity ────────────────────────────┐
│ ⟳ crawl  oleoolivia  34/120 pages  ▓▓▓░░ [✕]   │ │ status fora: design → waiting  1h    │
└────────────────────────────────────────────────┘ └──────────────────────────────────────┘
```

### 6.4 Sites list

```
Sites                                             [Table|Cards]  [Save view]  [+ Add site]
[🔍 search…        ] [Status ▾ 2] [Category ▾] [Tags ▾] [CMS ▾] [Fetched ▾] [More ▾]  ✕ Clear
Status: design (4) waiting (3) … chips of active filters with ✕
┌──┬─────────────────────────┬────────────┬──────────┬───────────┬────────────┬─────────┬──────┐
│☐ │ Domain                  │ CMS        │ Status   │ Category  │ Tags       │ Fetched │ To-do│
├──┼─────────────────────────┼────────────┼──────────┼───────────┼────────────┼─────────┼──────┤
│☐ │ ◉ forazeytin.com.tr     │ ▣ Custom   │ ● design │ Food      │ seo rebuild│ 2h ago  │ 3    │
│  │   Fora Zeytin           │    PHP 7.4 │          │           │            │         │      │
│☐ │ ◉ renama.com.tr         │ ▣ WordPress│ ● waiting│ Industry  │ wp         │ 3d ago •│ 1    │
└──┴─────────────────────────┴────────────┴──────────┴───────────┴────────────┴─────────┴──────┘
[ 2 selected:  Set status ▾  Set category ▾  + Tag  ⟳ Re-fetch  Archive ]        1–50 of 63
```

### 6.5 Add site dialog

```
┌ Add site ─────────────────────────────────────────────┐
│ URL  [ forazeytin.com.tr                            ]  │
│      → https://forazeytin.com.tr/   key: forazeytin.com.tr
│      ⚠ Already in your list: forazeytin.com.tr  [Open] │
│ Name      [ Fora Zeytin           ]  (optional)        │
│ Status    [ none ▾ ]   Category [ Food ▾ ]             │
│ Tags      [ seo × ] [ + ]                              │
│ ☑ Fetch profile now     ☐ Allow local/LAN target       │
│                                   [Cancel]  [Add site] │
└────────────────────────────────────────────────────────┘
```

### 6.6 Site detail: Profile tab

```
◉ forazeytin.com.tr ⧉   Fora Zeytin ✎    ↗ live site
[● design ▾] [Food ▾] [seo ×][rebuild ×][+]          [⟳ Re-fetch] [⇩ Download content] [⋯]
Profile · Notes 3 · To-dos 2/5 · Content 1 · History 4 · Activity
──────────────────────────────────────────────────────────────────── ┌ Project ───────────┐
⚠ Partial: Wayback timed out · NS lookup failed (registry NS shown)  │ Deadline 31 Oct    │
┌HTTP──┐┌CMS─────┐┌Hosting──┐┌Server──┐┌SSL──────┐┌Domain───┐┌Age──┐ │ Contacts           │
│ 200  ││ Custom ││Natro    ││LiteSpd ││ 61 days ││ 2027-07 ││ 26y │ │  Ali K. ✉ ☎ ◔      │
│ 0 ↪  ││ PHP7.4 ││TR · ASN ││        ││ Sectigo ││ TRABIS  ││     │ │ Links              │
└──────┘└────────┘└─────────┘└────────┘└─────────┘└─────────┘└─────┘ │  ⌂ prod ⎇ repo ☁ host│
▸ Tech stack     [jQuery ●med] [Bootstrap ●med] [UA-xxxx ●high] …    │ Health             │
▸ Hosting / DNS / Email    A 89.19.30.15 ⧉   MX natrohost (Natro)   │  SSL 61d · Dom 254d│
   SPF ✓  DMARC ✗  DNSSEC ✗  CAA ✗                                   └────────────────────┘
▸ SEO & meta     Title 58/60 ▓▓▓▓▓░  Description 133 ✓  H1: 0 ⚠  Canonical ✗
▸ Performance    TTFB 420 ms · HTML 84 KB · gzip ✓ · http→https ✓
▸ Pages & sitemap  1 sitemap · 214 URLs  [table…]
▸ Contact & social  info@… ⧉  +90 … ⧉   ⓕ ⓘ
▸ Security headers  ✓ 1/6  missing: HSTS, CSP, XFO …
[Profile] [Raw JSON]
```

### 6.7 History and compare

```
Snapshots                                   Compare  [2026-10-26 14:02 ▾] ⇄ [2026-10-01 09:10 ▾]
● 2026-10-26 14:02  ok       38 s  3 changes   ┌ Technologies ───────────────────────────────┐
● 2026-10-01 09:10  partial  41 s  0 changes   │ + Cloudflare (Hosting/edge)                 │
● 2026-08-14 18:30  ok       22 s  —           │ − LiteSpeed (Server)                        │
                                               ├ DNS ────────────────────────────────────────┤
                                               │ A  89.19.30.15 → 104.21.4.9                 │
                                               │ NS ns1.natrohost.com → *.ns.cloudflare.com  │
                                               └ [ ] show unchanged   [Raw JSON diff]         ┘
```

### 6.8 Notes tab

```
[+ New note]  [🔍 ]          │ Kickoff call ✎                       [Edit|Split|Preview] Saved ✓
📌 Kickoff call      2d       │ ┌──────────────────────┬──────────────────────────────────┐
   Content audit     5d       │ │ ## Goals             │  Goals                           │
   Hosting migration 1w       │ │ - New WP theme       │  • New WP theme                  │
                              │ │ - [ ] Logo files     │  ☐ Logo files                    │
                              │ └──────────────────────┴──────────────────────────────────┘
```

### 6.9 Content tab (crawl)

```
Crawls                      │ 2026-10-26 15:10 · running ⟳                         [✕ Cancel]
● 10-26 15:10 running 34p   │ ▓▓▓▓▓▓▓░░░░░░░  34 / ~120 pages   212 images · 6 files · 48 MB
● 09-30 11:02 done 88p 210MB│ Now: /urunler/siyah-zeytin.php        Elapsed 2:41 · ETA ~6 min
[+ New crawl]               │ ┌ Log ────────────────────────────────── [info ▾] ──────────┐
                            │ │ 15:12:31 page  /urunler/… (14 images)                      │
                            │ │ 15:12:33 skip  /sepet (exclude pattern)                    │
                            │ └────────────────────────────────────────────────────────────┘
(after done) [Pages] [Images] [Files] [Structure]   [⇩ ZIP] [📂 Open folder] [⧉ Path] [🗑]
```

### 6.10 Key flows

1. **Add & profile:** `+ Add site` → type URL → (live normalise and duplicate check) → Add → redirect to detail → progress card with phases → toast "Profile fetched" → sections render. On failure, a banner shows the reason and Re-fetch.
2. **Triage:** Sites → filter `status=waiting` + `tag=seo` → select 5 → `Set status: design` → activity entries created → "Save view: SEO in design".
3. **Re-fetch & compare:** Detail → `Re-fetch` → job done → toast "3 changes" → click → History compare opens on latest vs previous.
4. **Content export for a rebuild:** Detail → `Download content` → options (max pages 200, docs on) → Start → progress, then Done → Pages and Images tabs → `Copy path for agent` → paste it into the coding agent prompt ("read README.md at …").
5. **Daily planning:** Dashboard → overdue to-dos → tick off → open the site with the closest deadline.

---

## 7. Data model

### 7.1 ER overview

```
categories 1─┐
             ├─< sites >─┬─< site_tags >── tags
saved_views  │           ├─< profile_snapshots ─< snapshot_technologies
settings     │           ├─< notes
             │           ├─< todos
             │           ├─< contacts            (v1.1: → clients)
             │           ├─< site_links
             │           ├─< activity
             │           ├─< crawls ── jobs
             │           └─< jobs (profile | crawl | archive | backup)
             └ v1.1: access_items, offers, invoices, payments, milestones, attachments, templates
schema_migrations
```

`sites.latest_snapshot_id` points to the newest snapshot. Filterable profile facts (CMS, server, hosting, SSL and domain expiry, HTTP status) are **copied onto `sites`** when a snapshot is ingested, so the list query never touches JSON.

### 7.2 SQLite schema (migration `0001_init.sql`)

Conventions:
- timestamps are ISO-8601 UTC text (`strftime('%Y-%m-%dT%H:%M:%fZ','now')`); dates are `YYYY-MM-DD`
- `PRAGMA foreign_keys=ON`, `journal_mode=WAL`, `busy_timeout=5000` on every connection
- paths are stored **relative to the data dir** in POSIX form

```sql
CREATE TABLE schema_migrations (
  version     INTEGER PRIMARY KEY,
  name        TEXT NOT NULL,
  applied_at  TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now'))
);

CREATE TABLE categories (
  id          INTEGER PRIMARY KEY,
  name        TEXT NOT NULL UNIQUE COLLATE NOCASE CHECK (length(name) BETWEEN 1 AND 60),
  color       TEXT,                                   -- token name, e.g. 'emerald'
  sort_order  INTEGER NOT NULL DEFAULT 0,
  created_at  TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now'))
);

CREATE TABLE tags (
  id          INTEGER PRIMARY KEY,
  name        TEXT NOT NULL UNIQUE COLLATE NOCASE CHECK (length(name) BETWEEN 1 AND 40),
  color       TEXT,
  created_at  TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now'))
);

CREATE TABLE sites (
  id                  INTEGER PRIMARY KEY,
  site_key            TEXT NOT NULL UNIQUE,            -- host w/o leading www., punycode
  registrable_domain  TEXT,                            -- eTLD+1 (PSL)
  url                 TEXT NOT NULL,                   -- normalised production URL
  name                TEXT CHECK (name IS NULL OR length(name) <= 120),
  status              TEXT NOT NULL DEFAULT 'none'
                      CHECK (status IN ('none','design','waiting','done','canceled')),
  category_id         INTEGER REFERENCES categories(id) ON DELETE SET NULL,
  deadline            TEXT,                            -- YYYY-MM-DD
  allow_private_net   INTEGER NOT NULL DEFAULT 0,
  -- denormalised from latest snapshot (filter/sort columns)
  latest_snapshot_id  INTEGER REFERENCES profile_snapshots(id) ON DELETE SET NULL,
  last_fetched_at     TEXT,
  last_fetch_outcome  TEXT CHECK (last_fetch_outcome IN ('ok','partial','failed')),
  http_status         INTEGER,
  final_url           TEXT,
  title               TEXT,
  cms                 TEXT,                            -- e.g. 'WordPress', 'Wix', 'Custom'
  cms_version         TEXT,
  server              TEXT,
  hosting             TEXT,                            -- edge/hosting org, e.g. 'Cloudflare', 'Natro'
  backend_language    TEXT,
  ip_address          TEXT,
  tls_valid_until     TEXT,
  domain_expires_on   TEXT,
  registrar           TEXT,
  favicon_url         TEXT,
  archived_at         TEXT,
  created_at          TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')),
  updated_at          TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now'))
);
CREATE INDEX idx_sites_status      ON sites(status) WHERE archived_at IS NULL;
CREATE INDEX idx_sites_category    ON sites(category_id);
CREATE INDEX idx_sites_cms         ON sites(cms COLLATE NOCASE);
CREATE INDEX idx_sites_fetched     ON sites(last_fetched_at);
CREATE INDEX idx_sites_deadline    ON sites(deadline) WHERE deadline IS NOT NULL;
CREATE INDEX idx_sites_regdomain   ON sites(registrable_domain);

CREATE TABLE site_tags (
  site_id  INTEGER NOT NULL REFERENCES sites(id) ON DELETE CASCADE,
  tag_id   INTEGER NOT NULL REFERENCES tags(id)  ON DELETE CASCADE,
  PRIMARY KEY (site_id, tag_id)
) WITHOUT ROWID;
CREATE INDEX idx_site_tags_tag ON site_tags(tag_id);

CREATE TABLE profile_snapshots (
  id               INTEGER PRIMARY KEY,
  site_id          INTEGER NOT NULL REFERENCES sites(id) ON DELETE CASCADE,
  job_id           INTEGER REFERENCES jobs(id) ON DELETE SET NULL,
  fetched_at       TEXT NOT NULL,                      -- scanner's scanned_at_utc
  outcome          TEXT NOT NULL CHECK (outcome IN ('ok','partial','failed')),
  schema_version   INTEGER NOT NULL,
  tool_version     TEXT,
  requested_url    TEXT NOT NULL,
  final_url        TEXT,
  http_status      INTEGER,
  duration_ms      INTEGER,
  error_summary    TEXT,                               -- short human text for list/banner
  changes_count    INTEGER,                            -- vs previous snapshot, computed at ingest
  summary_json     TEXT NOT NULL CHECK (json_valid(summary_json)),  -- normalised (ProfileNormalizer)
  raw_json         TEXT NOT NULL CHECK (json_valid(raw_json)),      -- script output as-is
  created_at       TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now'))
);
CREATE INDEX idx_snapshots_site ON profile_snapshots(site_id, fetched_at DESC);

CREATE TABLE snapshot_technologies (
  snapshot_id  INTEGER NOT NULL REFERENCES profile_snapshots(id) ON DELETE CASCADE,
  category     TEXT NOT NULL,
  name         TEXT NOT NULL,
  version      TEXT,
  confidence   TEXT NOT NULL CHECK (confidence IN ('low','medium','high')),
  PRIMARY KEY (snapshot_id, category, name)
) WITHOUT ROWID;
CREATE INDEX idx_snaptech_name ON snapshot_technologies(name COLLATE NOCASE);

CREATE TABLE notes (
  id          INTEGER PRIMARY KEY,
  site_id     INTEGER NOT NULL REFERENCES sites(id) ON DELETE CASCADE,
  title       TEXT NOT NULL CHECK (length(title) BETWEEN 1 AND 200),
  body_md     TEXT NOT NULL DEFAULT '' CHECK (length(body_md) <= 200000),
  pinned      INTEGER NOT NULL DEFAULT 0,
  created_at  TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')),
  updated_at  TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now'))
);
CREATE INDEX idx_notes_site ON notes(site_id, pinned DESC, updated_at DESC);

CREATE TABLE todos (
  id          INTEGER PRIMARY KEY,
  site_id     INTEGER NOT NULL REFERENCES sites(id) ON DELETE CASCADE,
  title       TEXT NOT NULL CHECK (length(title) BETWEEN 1 AND 300),
  detail      TEXT,
  done        INTEGER NOT NULL DEFAULT 0,
  done_at     TEXT,
  due_date    TEXT,
  priority    TEXT NOT NULL DEFAULT 'normal' CHECK (priority IN ('low','normal','high')),
  sort_order  REAL NOT NULL DEFAULT 0,                 -- fractional ordering for cheap reorder
  created_at  TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')),
  updated_at  TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now'))
);
CREATE INDEX idx_todos_site ON todos(site_id, done, sort_order);
CREATE INDEX idx_todos_open_due ON todos(due_date) WHERE done = 0;

CREATE TABLE contacts (
  id                 INTEGER PRIMARY KEY,
  site_id            INTEGER NOT NULL REFERENCES sites(id) ON DELETE CASCADE,
  name               TEXT NOT NULL,
  role               TEXT,
  email              TEXT,
  phone              TEXT,
  preferred_channel  TEXT CHECK (preferred_channel IN ('email','phone','whatsapp','telegram','other')),
  notes              TEXT,
  created_at         TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')),
  updated_at         TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now'))
);

CREATE TABLE site_links (
  id          INTEGER PRIMARY KEY,
  site_id     INTEGER NOT NULL REFERENCES sites(id) ON DELETE CASCADE,
  kind        TEXT NOT NULL CHECK (kind IN ('production','staging','repo','hosting','registrar',
                                            'dns','analytics','design','other')),
  label       TEXT,
  url         TEXT NOT NULL,
  sort_order  INTEGER NOT NULL DEFAULT 0
);

CREATE TABLE activity (
  id          INTEGER PRIMARY KEY,
  site_id     INTEGER REFERENCES sites(id) ON DELETE CASCADE,
  type        TEXT NOT NULL,                           -- e.g. 'status.changed'
  message     TEXT NOT NULL,
  data_json   TEXT CHECK (data_json IS NULL OR json_valid(data_json)),
  created_at  TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now'))
);
CREATE INDEX idx_activity_site ON activity(site_id, id DESC);

CREATE TABLE jobs (
  id                INTEGER PRIMARY KEY,
  type              TEXT NOT NULL CHECK (type IN ('profile','crawl','archive','backup','selfcheck')),
  site_id           INTEGER REFERENCES sites(id) ON DELETE CASCADE,
  status            TEXT NOT NULL DEFAULT 'queued'
                    CHECK (status IN ('queued','running','succeeded','partial','failed','canceled')),
  params_json       TEXT NOT NULL DEFAULT '{}' CHECK (json_valid(params_json)),
  progress_current  INTEGER,
  progress_total    INTEGER,
  progress_label    TEXT,                              -- e.g. 'dns', '/about-us'
  stats_json        TEXT CHECK (stats_json IS NULL OR json_valid(stats_json)),   -- live counters
  result_json       TEXT CHECK (result_json IS NULL OR json_valid(result_json)),
  error             TEXT,
  exit_code         INTEGER,
  cancel_requested  INTEGER NOT NULL DEFAULT 0,
  worker_pid        INTEGER,
  child_pid         INTEGER,
  heartbeat_at      TEXT,
  attempts          INTEGER NOT NULL DEFAULT 0,
  log_path          TEXT,                              -- relative to data dir
  created_at        TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')),
  started_at        TEXT,
  finished_at       TEXT
);
CREATE INDEX idx_jobs_status ON jobs(status, created_at);
CREATE INDEX idx_jobs_site   ON jobs(site_id, created_at DESC);

CREATE TABLE crawls (
  id            INTEGER PRIMARY KEY,
  site_id       INTEGER NOT NULL REFERENCES sites(id) ON DELETE CASCADE,
  job_id        INTEGER REFERENCES jobs(id) ON DELETE SET NULL,
  output_dir    TEXT NOT NULL UNIQUE,                  -- 'crawls/<site-key>/<ts>'
  options_json  TEXT NOT NULL CHECK (json_valid(options_json)),
  status        TEXT NOT NULL CHECK (status IN ('running','done','partial','failed','canceled')),
  pages_count   INTEGER NOT NULL DEFAULT 0,
  images_count  INTEGER NOT NULL DEFAULT 0,
  files_count   INTEGER NOT NULL DEFAULT 0,
  bytes_total   INTEGER NOT NULL DEFAULT 0,
  archive_path  TEXT,                                  -- cached ZIP, relative
  started_at    TEXT NOT NULL,
  finished_at   TEXT
);
CREATE INDEX idx_crawls_site ON crawls(site_id, started_at DESC);

CREATE TABLE saved_views (
  id          INTEGER PRIMARY KEY,
  name        TEXT NOT NULL UNIQUE COLLATE NOCASE,
  query       TEXT NOT NULL,                           -- URL query string, e.g. 'status=waiting&tag=seo'
  sort_order  INTEGER NOT NULL DEFAULT 0,
  created_at  TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now'))
);

CREATE TABLE settings (
  key    TEXT PRIMARY KEY,
  value  TEXT NOT NULL                                 -- JSON
) WITHOUT ROWID;
```

Forward references (`sites` → `profile_snapshots`, `profile_snapshots` → `jobs`) are fine because SQLite resolves foreign-key targets when rows are written, not when tables are created. The `sites` ↔ `profile_snapshots` cycle is safe: deleting a site cascades to its snapshots, and deleting a snapshot nulls `latest_snapshot_id` (the API then repoints it to the previous snapshot).

### 7.3 Normalised profile summary (`summary_json`)

Written by `ProfileNormalizer` for schema v1 (existing files) and v2. The UI and the diff use only this object:

```jsonc
{
  "outcome": "partial",
  "errors": [{ "section": "archive", "code": "timeout", "message": "Wayback timed out" }],
  "url": { "requested": "https://forazeytin.com.tr/", "final": "https://forazeytin.com.tr/", "redirects": [] },
  "http": { "status": 200, "ttfbMs": 420, "totalMs": 910, "htmlBytes": 84211, "compression": "gzip",
            "httpsRedirect": true, "wwwCanonical": "apex", "blocked": false },
  "tech": [{ "category": "CMS", "name": "WordPress", "version": "6.6", "confidence": "high", "evidence": ["…"] }],
  "cms": { "name": "WordPress", "version": "6.6", "theme": "astra", "plugins": ["elementor", "yoast"] },
  "hosting": { "ips": ["89.19.30.15"], "ipv6": [], "org": "Natro", "asn": "AS34619", "reverseDns": "…",
               "edge": "Cloudflare", "server": "LiteSpeed", "language": "PHP", "languageVersion": "7.4.33" },
  "dns": { "ns": ["…"], "nsSource": "dns|registry", "mx": [{ "pref": 1, "host": "…" }], "emailProvider": "Natro",
           "spf": "v=spf1 …", "dmarc": null, "dnssec": false, "caa": [], "txt": ["…"] },
  "tls": { "verified": true, "issuer": "Sectigo", "validFrom": "…", "validUntil": "…", "daysLeft": 61,
           "sanCount": 2, "sans": ["…"], "fingerprint": "…", "error": null },
  "domain": { "registrant": "Zepa A.Ş.", "registrar": "…", "transferLocked": true, "created": "2000-07-06", "expires": "2027-07-05", "updated": null,
              "ageYears": 26.3, "statuses": ["Active"], "method": "WHOIS", "source": "…" },
  "seo": { "title": "…", "titleLength": 58, "description": "…", "descriptionLength": 133, "canonical": null,
           "robots": null, "noindex": false, "lang": "tr", "hreflang": [], "og": { "title": null, "image": null },
           "twitter": {}, "structuredData": [], "h1Count": 0, "headings": [{ "level": 2, "text": "…" }],
           "imagesTotal": 61, "imagesMissingAlt": 3 },
  "sitemaps": { "robotsStatus": 200, "disallowRules": 0, "files": [{ "url": "…", "kind": "urlset", "urls": 214 }],
                "totalUrls": 214, "urls": [{ "loc": "…", "lastmod": "…" }] },
  "contact": { "emails": ["…"], "phones": ["…"], "address": null, "socials": [{ "network": "instagram", "url": "…" }] },
  "security": { "present": ["X-Content-Type-Options"], "missing": ["Strict-Transport-Security", "…"],
                "cookies": [{ "name": "PHPSESSID", "secure": true, "httpOnly": true, "sameSite": null }] },
  "archive": { "first": "2009-03-01", "firstUrl": "…", "last": "2026-07-10", "lastUrl": "…" },
  "favicon": "https://…/favicon.ico",
  "limitations": ["…"]
}
```

### 7.4 v1.1 tables (planned, not in `0001`)

- `clients (id, name, company, notes)`, plus `contacts.client_id` and `sites.client_id`
- `access_items (id, site_id, kind, label, state, username, secret_ref, notes, updated_at)`
- `offers (id, site_id, amount_cents, currency, status, sent_on, decided_on, notes)`
- `invoices (id, site_id, number, amount_cents, currency, issued_on, due_on, status)`
- `payments (id, invoice_id, paid_on, amount_cents, method)`
- `milestones (id, site_id, title, due_date, done_at)`
- `attachments (id, site_id, file_path, original_name, mime, bytes, created_at)`
- `templates (id, kind, name, body_json)`
- `search_index` (FTS5, content-less, filled by triggers)
- `reminders_snooze (key, until)`

Money is always stored in integer minor units with a currency code.

---

## 8. API design

### 8.1 Conventions
- **Base path:** `/api/…`. In production, `dist/index.php` (front controller) dispatches `/api/*` to the PHP router and serves `index.html` for every other path (SPA deep links). The fallback for servers without front-controller rewriting is `/index.php/api/...` or `/backend/index.php?r=/...`; see D9 and Open Question 2.
- JSON in and out, UTF-8; keys are **camelCase**; timestamps are ISO-8601 UTC; dates are `YYYY-MM-DD`.
- **Errors:** `{"error": {"code": "validation_failed", "message": "Human readable", "fields": {"url": "Enter a valid URL"}}}` with HTTP status 400 (bad input), 404, 409 (duplicate or conflict), 413, 422 (semantic), 429 (concurrency limit), or 500 (with a `logRef` id pointing to `data/logs/app.log`).
- **Pagination:** `?page=1&perPage=50` returns `{ "items": [...], "total": 63, "page": 1, "perPage": 50 }`.
- **Mutations** (`POST`, `PATCH`, `PUT`, `DELETE`) require `Content-Type: application/json` and the header `X-Requested-With: freelance-dashboard`, and the `Host`/`Origin` must be in the allow-list (§11).
- **Long work** returns `202 Accepted` with `{ "job": Job }`. The client polls `GET /api/jobs/:id`.

### 8.2 Core types (TypeScript view)

```ts
type Status = "none" | "design" | "waiting" | "done" | "canceled"
type Outcome = "ok" | "partial" | "failed"
type JobStatus = "queued" | "running" | "succeeded" | "partial" | "failed" | "canceled"

interface SiteListItem {
  id: number; siteKey: string; url: string; name: string | null; faviconUrl: string | null
  status: Status; category: { id: number; name: string; color: string | null } | null
  tags: { id: number; name: string; color: string | null }[]
  cms: string | null; cmsVersion: string | null; hosting: string | null
  lastFetchedAt: string | null; lastFetchOutcome: Outcome | null
  tlsValidUntil: string | null; domainExpiresOn: string | null
  openTodos: number; deadline: string | null; archivedAt: string | null; updatedAt: string
}
interface SiteDetail extends SiteListItem {
  registrableDomain: string | null; finalUrl: string | null; title: string | null
  latestSnapshot: SnapshotMeta | null; activeJob: Job | null
  contacts: Contact[]; links: SiteLink[]
  counts: { notes: number; todosOpen: number; todosDone: number; crawls: number; snapshots: number }
}
interface SnapshotMeta { id: number; fetchedAt: string; outcome: Outcome; durationMs: number | null
  httpStatus: number | null; errorSummary: string | null; changesCount: number | null; schemaVersion: number }
interface Snapshot extends SnapshotMeta { summary: ProfileSummary /* §7.3 */; raw?: unknown }
interface Job { id: number; type: "profile" | "crawl" | "archive" | "backup" | "selfcheck"; siteId: number | null
  status: JobStatus; progress: { current: number | null; total: number | null; label: string | null }
  stats: Record<string, number> | null; error: string | null; cancelRequested: boolean
  createdAt: string; startedAt: string | null; finishedAt: string | null; result: unknown | null }
```

### 8.3 Endpoints

**System**
| Method | Path | Body / query | Response |
| --- | --- | --- | --- |
| GET | `/api/health` | | `{ ok, version, env: { os, php, extensions{}, sqlite, fts5, dataDir, dataDirWritable, python: { path, version, ok }, phpCli: { path, ok } }, migrations: { current, latest } }` |
| GET | `/api/dashboard` | | `{ statusCounts, deadlines[], openTodos[], expiring: { tls[], domains[] }, recentFetches[], jobs[], activity[] }` |
| POST | `/api/backups` | | `201 { file, bytes, createdAt }` (synchronous `VACUUM INTO`, < 1 s for this size) |
| GET | `/api/backups` · `/api/backups/:name/download` | | list / file |

**Sites**
| Method | Path | Body / query | Response |
| --- | --- | --- | --- |
| GET | `/api/sites` | `q, status (csv), category (csv ids), tag (csv), tagMode=any\|all, cms (csv), tech, fetched=from..to, outcome (csv incl. never), hasOpenTodos, deadline=from..to, archived=0\|1, sort=-fetched,domain, page, perPage` | `{ items: SiteListItem[], total, page, perPage, facets: { status: {design: 4,…}, cms: {…}, category: {…}, tag: {…} } }` |
| GET | `/api/sites/check-url` | `url` | `{ normalizedUrl, siteKey, registrableDomain, blockedReason: null\|'private_network', existing: {id, siteKey}\|null, related: [{id, siteKey}] }` |
| POST | `/api/sites` | `{ url, name?, status?, categoryId?, tags?: string[], fetch?: true, allowPrivateNet?: false, allowDuplicate?: false }` | `201 { site: SiteDetail, job: Job\|null }` · `409 { error: { code: 'duplicate', existing } }` |
| GET | `/api/sites/:id` | | `SiteDetail` |
| PATCH | `/api/sites/:id` | any of `{ name, url, status, categoryId, tags, deadline, allowPrivateNet }` | `SiteDetail` (activity written for status, category and tag changes) |
| DELETE | `/api/sites/:id` | `?mode=archive` (default) or `?mode=delete&confirm=<siteKey>` | `204` (delete also removes crawl folders, via a job if large) |
| POST | `/api/sites/:id/restore` | | `SiteDetail` |
| POST | `/api/sites/bulk` | `{ ids: number[], action: 'setStatus'\|'setCategory'\|'addTag'\|'removeTag'\|'archive'\|'refetch', value? }` | `{ updated: number, jobs?: Job[] }` |

**Profiles / snapshots**
| Method | Path | Body / query | Response |
| --- | --- | --- | --- |
| POST | `/api/sites/:id/fetch` | `{}` | `202 { job }` · `409` if a profile job is already active for the site |
| GET | `/api/sites/:id/snapshots` | `page` | `{ items: SnapshotMeta[] … }` |
| GET | `/api/snapshots/:id` | `?raw=1` | `Snapshot` |
| GET | `/api/snapshots/:id/diff` | `against=<id>` (default: previous) | `{ a: SnapshotMeta, b: SnapshotMeta, groups: [{ section: 'Technologies', changes: [{ path, label, kind: 'added'\|'removed'\|'changed', before, after }] }], unchangedSections: string[] }` |
| DELETE | `/api/snapshots/:id` | | `204` (the latest pointer moves to the previous snapshot) |

**Notes / to-dos / activity**
| Method | Path | Body / query | Response |
| --- | --- | --- | --- |
| GET / POST | `/api/sites/:id/notes` | POST `{ title, bodyMd?, pinned? }` | `Note[]` / `201 Note` |
| GET / PATCH / DELETE | `/api/notes/:id` | PATCH `{ title?, bodyMd?, pinned?, updatedAt }` (optimistic concurrency: `409` if stale) | `Note` |
| GET / POST | `/api/sites/:id/todos` | POST `{ title, dueDate?, priority?, detail? }` | `Todo[]` / `201 Todo` |
| PATCH / DELETE | `/api/todos/:id` | `{ title?, done?, dueDate?, priority?, detail?, sortOrder? }` | `Todo` |
| GET | `/api/todos` | `status=open\|done\|all, site, priority, due=overdue\|today\|week\|none, page` | `{ items: (Todo & { site: {id, siteKey, name} })[], total }` |
| GET | `/api/sites/:id/activity` · `/api/activity` | `before=<id>&limit=30` | `{ items: Activity[], nextBefore }` |

**Contacts / links / taxonomy / views**
| Method | Path | Notes |
| --- | --- | --- |
| GET/POST | `/api/sites/:id/contacts` · PATCH/DELETE `/api/contacts/:id` | validation: email format, E.164-ish phone (lenient) |
| GET/POST | `/api/sites/:id/links` · PATCH/DELETE `/api/links/:id` | URL must be http(s) |
| GET/POST | `/api/categories` · PATCH/DELETE `/api/categories/:id` | delete sets `sites.category_id = NULL` |
| GET/POST | `/api/tags` · PATCH/DELETE `/api/tags/:id` · POST `/api/tags/:id/merge` `{ intoId }` | |
| GET/POST | `/api/views` · PATCH/DELETE `/api/views/:id` | `{ name, query }` |

**Jobs**
| Method | Path | Body / query | Response |
| --- | --- | --- | --- |
| GET | `/api/jobs` | `status=active\|finished\|all, type, site, page` | `{ items: Job[] … }` |
| GET | `/api/jobs/:id` | | `Job` (cheap; polled at 1 s) |
| GET | `/api/jobs/:id/log` | `offset=<bytes>&limit=65536` | `{ text, nextOffset, eof }` (tail-follow) |
| POST | `/api/jobs/:id/cancel` | | `202 Job` (`cancelRequested: true`) |
| POST | `/api/jobs/:id/retry` | | `202 { job }` (a new job with the same params) |

**Crawls**
| Method | Path | Body / query | Response |
| --- | --- | --- | --- |
| POST | `/api/sites/:id/crawls` | `{ startUrls?, maxPages, maxDepth, includeSubdomains, include[], exclude[], respectRobots, delay, concurrency, types: {images, documents, media, fonts}, extensions?, maxFileMb, maxTotalMb, mainSelector?, strip[], keepChrome, languages[], wordpress }` | `202 { crawl, job }` · `429` if a crawl is already running |
| GET | `/api/sites/:id/crawls` | | `Crawl[]` |
| GET | `/api/crawls/:id` | | `Crawl & { manifest }` |
| GET | `/api/crawls/:id/pages` | `q` | `{ items: [{ id, url, file, title, depth, wordCount, images }], tree }` |
| GET | `/api/crawls/:id/pages/:pageId` | | `{ meta, markdown }` |
| GET | `/api/crawls/:id/assets` | `kind=image\|file\|media, pageId, minWidth, page, perPage` | `{ items: Asset[] … }` |
| GET | `/api/crawls/:id/raw` | `path=images/foo.jpg` (validated, §11) | streamed file with the correct `Content-Type`, `Content-Disposition` for downloads, `nosniff` |
| POST | `/api/crawls/:id/archive` | | `202 { job }` or `200 { ready: true }` if cached |
| GET | `/api/crawls/:id/archive` | | ZIP stream |
| POST | `/api/crawls/:id/reveal` | | `204` (opens Explorer or a file manager on the host); `501` if unsupported |
| DELETE | `/api/crawls/:id` | | `204` |

---

## 9. Background job design

### 9.1 Why
php-cgi requests must finish quickly. On Windows, `max_execution_time` counts **wall-clock** time, and Nginx has `fastcgi_read_timeout`. A profile takes 10–40 s and a crawl takes minutes. So the HTTP request only **enqueues** work, and a separate **PHP CLI worker** process (no time limit) does it.

### 9.2 Components

```
 Browser ──POST /api/sites/12/fetch──▶ php-cgi (API)
                                         │ 1. INSERT jobs(status=queued)
                                         │ 2. JobLauncher::ensureWorker()  ──spawn detached──┐
                                         ▼                                                   │
                                  202 {job}                                                   ▼
 Browser ──GET /api/jobs/:id (1 s)──▶ php-cgi reads jobs row          php.exe backend/bin/worker.php
                                                                          │ loop: claim next queued job
                                                                          │   (UPDATE … WHERE status='queued'
                                                                          │    AND <concurrency ok> RETURNING)
                                                                          │ write data/jobs/<id>/spec.json
                                                                          │ proc_open([python, script, --spec, …])  (array form, no shell)
                                                                          │ read stdout NDJSON events → UPDATE jobs (progress, stats, heartbeat)
                                                                          │ append stdout/stderr text → data/jobs/<id>/job.log
                                                                          │ every 1 s: check cancel_requested → kill tree
                                                                          │ on exit: ingest result (snapshot / crawl row), set status, activity
                                                                          │ no more queued jobs for 10 s → exit
```

- **Queue = the `jobs` table.** No extra service is needed.
- **Worker** = `backend/bin/worker.php`:
  - runs with `set_time_limit(0)` and `ignore_user_abort(true)`
  - holds an exclusive lock file `data/run/worker.lock` (`flock`) so only one worker is active at a time; it runs jobs concurrently as child processes up to the limits
  - **concurrency limits:** 3 `profile`, 1 `crawl`, 1 `archive`
  - when idle for 10 s it exits, so nothing stays running in the background when no work is queued
- **Launcher** (`JobLauncher::ensureWorker()`): called after enqueueing **and** on every `GET /api/jobs*` and `GET /api/dashboard`. If the lock is free (no live worker) and queued jobs exist, it spawns one. This is self-healing: a worker killed by a reboot is restarted by the next page load.
- **Child process contract (Python):**
  - `--spec data/jobs/<id>/spec.json`: inputs as JSON, so no user data goes into argv beyond fixed paths
  - `--events`: NDJSON on stdout, for example `{"type":"progress","current":34,"total":120,"label":"/about"}`, `{"type":"stats","pages":34,"images":212,"bytes":50331648}`, `{"type":"log","level":"warn","message":"…"}`, `{"type":"result","path":"…/profile.json"}`
  - stderr is free text (tracebacks) and goes to the log only
  - exit codes as in §5.4
- **Heartbeat & orphan recovery:**
  - the worker updates `jobs.heartbeat_at` every 2 s for each running job
  - the launcher marks `running` jobs with a heartbeat older than 30 s **and** no live `worker_pid` as `failed` ("worker stopped unexpectedly"), and they can be retried
  - crawls keep their partial output (`status: failed` in the manifest)
- **Timeouts:** profile 120 s; crawl configurable (default 2 h); archive 30 min. When exceeded, the worker kills the child and marks the job `failed: timeout`.
- **Cancel:**
  - `POST /api/jobs/:id/cancel` sets `cancel_requested=1`, and the worker notices within 1 s
  - it first sends a soft stop: on Linux `SIGTERM` to the process group; on Windows, it writes `data/jobs/<id>/cancel` and Python polls for that file between pages
  - after 5 s it hard-kills: Linux `SIGKILL` to the process group; Windows `taskkill /PID <child> /T /F`, run with the array form of `proc_open`
  - then it sets `status=canceled`, and the crawler's last atomic manifest write stays valid
- **Logs:** `data/jobs/<id>/job.log` (rotated at 10 MB), read through `/api/jobs/:id/log?offset=`. Old job folders are pruned after 30 days; crawl output is never auto-deleted.

### 9.3 Spawning a detached worker per OS

| | Windows (Appytizer php-cgi) | Linux (dev) |
| --- | --- | --- |
| PHP CLI binary | `config.php_cli`; default: `php.exe` next to `PHP_BINARY` (`php-cgi.exe`) | `config.php_cli`; default `PHP_BINARY` if it is CLI, else `php` |
| Spawn | `pclose(popen('start "" /B "<php.exe>" "<worker.php>"', 'r'))`. Only fixed, validated config paths are used (no `"`, `%` or `^` allowed; validated at boot). | `exec('setsid nohup <php> <worker.php> >/dev/null 2>&1 &')` with `escapeshellarg` on every part |
| Liveness | `flock` on `worker.lock` (if the lock can be acquired, no worker is alive) | same |
| Kill tree | `taskkill /PID n /T /F` | `posix_kill(-pgid, SIGTERM/SIGKILL)` |
| Fallback if spawn is not allowed or not reliable | Run `php backend/bin/worker.php --daemon` from **Task Scheduler** "at log on" (it polls the DB every 2 s, ~0 CPU). The launcher detects the daemon via the lock and does not spawn. | `pnpm dev:worker` (same daemon mode) |

> **Risk (M0 spike):** if Appytizer puts php-cgi in a Windows *Job Object* that kills child processes when php-cgi recycles, `start /B` children may die early. The spike verifies that a 3-minute dummy job survives php-cgi recycling. If it does not, the daemon fallback becomes the documented default, and the Settings → Health page shows "Worker: daemon not running" with the exact command to register it.

---

## 10. File and folder structure, environment and path strategy

### 10.1 Repository layout

```
freelance-dashboard/
├── backend/
│   ├── index.php                    # API front controller (routes /api/*)
│   ├── bootstrap.php                # autoload (PSR-4, no Composer at runtime), config, error handler
│   ├── bin/
│   │   ├── worker.php               # job worker (CLI only; refuses to run under cgi)
│   │   ├── migrate.php              # run migrations (also auto-run on first request after build)
│   │   ├── seed.php                 # demo data (--demo), idempotent
│   │   └── backup.php
│   ├── config/
│   │   └── config.example.php       # copy to /config.local.php (project root) to override
│   ├── migrations/
│   │   ├── 0001_init.sql
│   │   └── 0002_….sql
│   ├── resources/public_suffix_list.dat
│   ├── src/
│   │   ├── Http/        Router.php, Request.php, Response.php, Guard.php (host/origin/CSRF)
│   │   ├── Controllers/ Sites, Snapshots, Notes, Todos, Activity, Contacts, Links, Taxonomy,
│   │   │                Views, Jobs, Crawls, Dashboard, System
│   │   ├── Domain/      UrlNormalizer, PublicSuffixList, SiteQuery (filter→SQL builder),
│   │   │                ProfileNormalizer (v1+v2), ProfileIngestor, SnapshotDiff, ActivityLogger,
│   │   │                CrawlIngestor, CrawlReader
│   │   ├── Jobs/        JobRepository, Worker, JobLauncher, Process/{WindowsProcess,PosixProcess}
│   │   ├── Infra/       Config, Paths, Platform, Database, Migrator, Logger, Backup
│   │   └── Security/    SafePath, NetworkGuard
│   ├── tests/           (PHPUnit) Unit/…, Api/… (runs php -S against a temp data dir)
│   └── dev-router.php   # for `php -S` in dev
├── public/
│   └── index.php        # copied by Vite to dist/index.php: /api/* → backend, else index.html
├── scripts/
│   ├── pyproject.toml   # uv project: python>=3.11, deps for both tools, pytest (dev)
│   ├── uv.lock
│   ├── site_tools/      # shared: events.py (NDJSON), exitcodes.py, netguard.py (SSRF), spec.py
│   ├── site_profile/    # refactored profiler package (fetch, dns, tls, registration, whois_tr,
│   │                    #   html, tech, contacts, sitemaps, archive, schema.py)
│   ├── site_profile.py  # thin CLI shim → site_profile.cli:main (keeps current usage working)
│   ├── generic_crawler/ # crawler v2 (output, manifest, assets, structure modules added)
│   └── tests/           # pytest + saved HTML/WHOIS/sitemap fixtures (offline)
├── src/                 # React app
│   ├── main.tsx, router.tsx
│   ├── api/             client.ts (fetch wrapper, error type, CSRF header), types.ts, hooks/*.ts
│   ├── components/ui/   shadcn primitives
│   ├── components/      layout/, common/ (CopyButton, EmptyState, ErrorState, JsonTree, Markdown…)
│   ├── features/
│   │   ├── dashboard/   sites/ (list, filters, bulk, add-dialog)   site/ (detail shell)
│   │   ├── profile/     sections/*.tsx, diff/
│   │   ├── notes/       todos/   activity/   crawl/   jobs/   settings/
│   └── lib/             utils.ts, format.ts, search-params.ts
├── data/                # RUNTIME DATA (gitignored, never inside dist) – see 10.3
├── config.local.php     # optional machine overrides (gitignored)
├── docs/                # PRD.md, DECISIONS.md, IMPLEMENTATION_PLAN.md, prompt.md
├── vite.config.ts       # copy-backend plugin + build-info + safety asserts
└── package.json         # scripts: dev, dev:api, dev:worker, build, test, test:php, test:py, lint, typecheck
```

### 10.2 What `pnpm build` produces

```
dist/                          (junction → C:\appytizer\dashboard)
├── index.html, assets/…       # Vite output
├── index.php                  # front controller from /public
└── backend/                   # copy of /backend minus tests/, dev-router.php
    └── build-info.php         # generated: ['project_root' => 'D:\\…\\freelance-dashboard', 'built_at' => …, 'version' => …]
```

The build plugin:
1. copies `backend/**` except `tests/`, `data/` and `*.local.php`
2. writes `build-info.php` with the **absolute project root** (`realpath`)
3. **refuses to build** if the resolved data dir is inside `dist/`

Vite's `emptyOutDir` only ever deletes `dist/`, and there is no data there. The legacy `backend/data/` folder is ignored and migrated (see M0).

### 10.3 Config resolution (`Infra/Config`)

The first match wins for each key:
1. **Environment variables:** `APP_ENV` (`production`/`development`), `APP_DATA_DIR`, `APP_PYTHON`, `APP_PHP_CLI`, `APP_ALLOWED_HOSTS`.
2. **`<project_root>/config.local.php`**, which returns an array. `project_root` comes from `build-info.php` when running from `dist/`, or from `dirname(__DIR__)` when running from source.
3. **OS defaults** (`Platform::isWindows()` = `PHP_OS_FAMILY === 'Windows'`):

| Key | Windows default | Linux default |
| --- | --- | --- |
| `data_dir` | `<project_root>\data` | `<project_root>/data` |
| `python` | `<project_root>\scripts\.venv\Scripts\python.exe`, else `py -3` launcher, else `python` on PATH | `<project_root>/scripts/.venv/bin/python`, else `python3` |
| `php_cli` | `dirname(PHP_BINARY)\php.exe` | `PHP_BINARY` (if CLI) or `php` |
| `allowed_hosts` | `dashboard.test, localhost, 127.0.0.1` | `localhost, 127.0.0.1` + dev ports |

**Data dir contents:**

```
data/
├── app.sqlite (+ -wal, -shm)
├── backups/      app-2026-10-26T1402-pre-migration-0003.sqlite
├── jobs/<id>/    spec.json, job.log, profile.json, cancel
├── crawls/<site-key>/<YYYYMMDD-HHMMSS>/…
├── attachments/  (v1.1)
├── run/          worker.lock
└── logs/         app.log, worker.log  (daily rotation, 14 days)
```

**Path rules:**
- All filesystem access goes through `Paths::data('crawls', $key, $ts)`, which joins with `DIRECTORY_SEPARATOR` and rejects `..`.
- Paths go into the DB as POSIX relative paths and are converted on read.
- Site keys are sanitised for Windows: reserved names (`CON`, `NUL`…), trailing dots and spaces, and `<>:"|?*` are replaced.
- Python receives absolute paths through spec.json, so there is no cwd dependence. It uses `pathlib`, which handles both OSes.
- Long paths: crawl file names are capped at 100 characters and folders are kept shallow, so everything stays under Windows' 260-character `MAX_PATH`.

### 10.4 Running it

**Windows (production use)**
1. One-time: install Python ≥ 3.11 and `uv`, then run `uv sync --project scripts` (creates `scripts\.venv`). Enable `pdo_sqlite`, `intl`, `zip`, `mbstring`, `fileinfo` and `openssl` in Appytizer's `php.ini`.
2. `pnpm install && pnpm build` → open `http://dashboard.test`. Migrations run automatically, with a backup first.
3. Settings → Health shows ✓ for Python, PHP CLI, the data dir and the worker spawn test. (If the spawn test fails, register the Task Scheduler daemon; the command is shown on the page.)

**Linux (dev container)**
```bash
pnpm install
uv sync --project scripts
pnpm dev:api      # php -S 127.0.0.1:8787 backend/dev-router.php   (APP_ENV=development)
pnpm dev          # Vite on :5173, proxies /api → http://127.0.0.1:8787 (override: PHP_API_TARGET)
pnpm dev:worker   # optional: daemon worker in the foreground with live logs
pnpm test         # vitest + phpunit + pytest
```

---

## 11. Security (local-only, but sane)

| Threat | Mitigation |
| --- | --- |
| SQL injection | PDO prepared statements only. `SiteQuery` builds `WHERE` from an allow-list of filters and sort columns, and values are always bound. |
| XSS (notes, crawled Markdown, profile data from hostile sites) | React escapes by default. Markdown is rendered through `react-markdown` + `rehype-sanitize` (no raw HTML, `javascript:` URLs removed). Crawled `.md` gets the same pipeline. The JSON tree renders text nodes only. CSP header on `index.html`/API: `default-src 'self'; img-src 'self' data: https:; object-src 'none'; frame-ancestors 'none'`. |
| CSRF from other sites open in the browser (`http://dashboard.test` is reachable by any page) | Mutations require `Content-Type: application/json` **and** the custom header `X-Requested-With`, which forces a CORS preflight. The API never sends CORS allow headers. `Origin` (when present) must match an allowed host. No cookies or session are used at all. |
| DNS rebinding | Every request's `Host` header must be in `allowed_hosts`, otherwise `421`. |
| Command injection | No user input ever reaches a shell. Python is started with the array form of `proc_open` (no shell, PHP ≥ 7.4), and inputs go through `spec.json`. The only shell use is the Windows `start` spawn, which takes validated config paths and no user data. |
| Path traversal (crawl file browser, ZIP, reveal) | `SafePath::resolve($crawlDir, $relative)` rejects absolute paths, `..`, NUL and drive letters, `realpath()`s the result, and requires the `crawlDir` prefix. No symlinks are followed; the crawler never creates any. |
| SSRF (fetcher and crawler reaching LAN or localhost, including this app) | `netguard.py` resolves every host (initial and every redirect) and blocks loopback, private (RFC 1918), link-local, CGNAT, multicast and IPv6 ULA ranges, unless the site has `allowPrivateNet`. PHP `NetworkGuard` pre-checks in `check-url`. Only ports 80, 443 and 8080/8443 are allowed by default. |
| Resource exhaustion | Caps: per-request bytes (2 MB HTML), crawl max pages, size and duration, job concurrency, note size, upload size (v1.1). |
| Secrets | None stored in the MVP. Access items are references only (D7). |
| Error leakage | Production returns generic messages plus a `logRef`; details go to `data/logs/app.log`. Development includes the stack trace. |
| Hostile crawled files | Served with `X-Content-Type-Options: nosniff`. HTML and SVG from crawls are served as downloads (`Content-Disposition: attachment`) or as `text/plain`, never inline as active content. |

---

## 12. Non-functional requirements

- **Performance:**
  - the list API responds in under 100 ms for 1,000 sites
  - the detail page (without raw JSON) is under 150 KB of JSON
  - first load is under 300 KB gzipped JS (route-level code splitting: the profile, notes editor and crawl browser are lazy-loaded)
- **Reliability:**
  - WAL mode and a busy timeout handle the API and the worker writing at once
  - every multi-row change runs in a transaction
  - crawl metadata is written atomically
  - automatic pre-migration backup
- **Migrations:**
  - numbered `.sql` files in `backend/migrations/`, applied in order inside a transaction and recorded in `schema_migrations`
  - an optional `NNNN_name.php` exists for data migrations
  - never edit an applied migration
  - `bin/migrate.php --status` prints the state
- **Seed/demo data:** `bin/seed.php --demo` creates the 4 example sites from `scripts/generic_crawler/examples/*.json` as snapshots (exercising the v1 normaliser), plus notes, to-dos, tags and a saved view. It is idempotent.
- **Observability:**
  - `app.log` and `worker.log` as human-readable lines: `2026-10-26 14:02:11 ERROR [jobs#42] python exited 3: unreachable (DNS NXDOMAIN)`
  - the job log is visible in the UI
  - the Health page
- **Accessibility:** keyboard-reachable controls, visible focus rings (already in the shadcn tokens), `aria-live` for job progress, colour never the only signal (icons and text on status).
- **Browser:** current Chrome, Edge and Firefox.
- **i18n:** the UI is in English. Content can be any language and is UTF-8 throughout. Turkish characters are handled in slugs (already in the crawler's `fold()`).
- **Dependencies:** frontend additions are listed in D5 (about 8 packages). The PHP runtime needs no Composer packages; Composer is used only for PHPUnit in development. Python needs 6 runtime packages, pinned through `uv.lock`.
- **Testing (critical parts):**
  - **Python:** WHOIS/RDAP parsing, the TRABIS parser, tech detection, sitemap parsing, contact extraction, the SSRF guard, the crawler slug/link-rewrite/asset-dedupe/manifest writer. All offline, on fixtures.
  - **PHP:** UrlNormalizer + PSL, SiteQuery filter building, ProfileNormalizer (v1 and v2 fixtures), SnapshotDiff, SafePath, Migrator, the Guard (CSRF/Host), API integration tests through `php -S` on a temp data dir, and the job runner lifecycle with a fake Python script (queued → running → progress → succeeded / cancel / timeout / orphan).
  - **Frontend:** Vitest for search-param (de)serialisation, the Markdown sanitisation snapshot, and diff rendering.

---

## 13. Open questions (only those I can't decide myself)

1. **Python on your PC:** is Python ≥ 3.11 installed, and is it OK to require **`uv`** (one `uv sync` creates `scripts\.venv`)? If you'd rather not install anything, tell me, and I'll move the profiler to PHP first (see D1, option B) and keep Python only for the crawler.
2. **Appytizer details:**
   - (a) Do unknown paths fall back to `index.php` (Valet-style front controller)? This decides between clean `/api/...` URLs plus SPA deep links and the `?r=` fallback with hash routing.
   - (b) Is `php.exe` (CLI) next to `php-cgi.exe`, and which PHP version is it?
   - (c) Does php-cgi run as your Windows user? This matters for spawned workers and for "Open folder" launching Explorer.
3. **Data location:** is `<project>\data` fine, or do you want the DB and crawls somewhere else, e.g. under `D:\Projects\Freelance\`?
4. **Existing reports:** should I import the JSON files in `D:\Projects\Freelance\!Reports` as initial sites and snapshots (MVP), or start empty (the importer stays v1.1)?
5. **Categories:** what do categories mean for you (industry? client type? project type?). Should I seed a default list, or start empty?

---

## 14. Assumptions (tell me if any are wrong)

- The UI language is English.
- Statuses are exactly `none, design, waiting, done, canceled`. They aren't configurable in the MVP, but labels and colours are.
- One "site" = one project. A client with 3 domains gets 3 sites, linked by a shared contact, and by a shared client in v1.1.
- Under 1,000 sites, crawls up to a few thousand pages, a single user.
- Only sites you own or may copy are crawled. robots.txt is respected by default.
