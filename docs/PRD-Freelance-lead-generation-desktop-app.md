# PRD: Freelance lead generation desktop app

Nov 12, 2026 · @Burak

This app turns the existing `web-dashboard` site analyzer into a Qt/C++ desktop tool that finds businesses with weak websites, scores them as leads, and tracks outreach and client work in one place. It is a single-user tool for one freelance web developer working in Türkiye and internationally.

## Goals and non-goals

**Goals**

1. **Analyze** any website from a URL: run every existing `web-dashboard` check plus the new email/DNS, ad-tracker, and contact-channel checks.
2. **Discover** leads in bulk by niche and location instead of entering sites one at a time.
3. **Score** each lead so the best prospects (spends on customers, weak site, reachable owner) rise to the top.
4. **Reach out** with prepared email and WhatsApp messages, and track every contact and follow-up.
5. **Manage** won clients: projects, deadlines, to-dos, domain and SSL expiry.
6. **Stay up.** The current app crashes often; one failing check must never take down an analysis or the app.

**Non-goals for v1**

- Sending email or WhatsApp messages automatically. The app prepares messages; the user sends them.
- Scraping Google Maps, Instagram, or LinkedIn pages. Maps data comes from the official Places API; social profiles are opened by hand.
- Multi-user accounts, roles, or team sync.
- Invoicing and accounting.
- Mobile or web builds of the new app. `web-dashboard` stays in the repo as reference and as the source of the Python scripts.

## Core workflow

A lead moves through seven stages. Each stage is a status value on the lead and a column in the pipeline view.

1. **Discover.** The user runs a discovery job ("diş kliniği Kadıköy", an imported exporter list, or a single URL). Each result becomes a lead with status `new`.
2. **Analyze.** The app queues a full analysis per lead: existing checks, new checks, desktop and mobile screenshots.
3. **Score.** The scoring engine turns analysis results into a 0–100 score and a tier (A, B, C, or disqualified).
4. **Review.** The user opens the top leads, reads the generated observations, and marks each `qualified` or `rejected`.
5. **Prepare.** For a qualified lead the user builds a redesign concept outside the app, then records its preview URL on the lead.
6. **Contact.** The app fills a message template with the lead's data. The user sends it by email or WhatsApp, and the app logs the contact and schedules follow-ups.
7. **Convert.** A lead that replies moves to `in conversation`, then `won` or `lost`. A won lead becomes a client with projects.

Entering a single URL runs stages 2 and 3 immediately and shows the detail page as results arrive.

## Architecture

The Qt app replaces the React frontend and the PHP backend; the Python tools stay and keep their existing job contract. `web-dashboard/` remains in the repo as the reference implementation and the home of the Python code.

| Component | Technology | Responsibility |
| --- | --- | --- |
| Desktop shell | Qt 6, C++ (existing UI template) | Views, navigation, settings, health page |
| Data layer | C++ repository classes over Qt SQL | All reads and writes to the remote MySQL database |
| Job runner | C++ `JobRunner` built on `QProcess` | Replaces `backend/bin/worker.php`: queue, concurrency, timeouts, cancel, result ingestion |
| Python tools | `web-dashboard/scripts` (uv project, Python 3.11+) | `site_profile` (extended), `site_crawl`, new `site_render` and `lead_discover` |
| Scoring engine | C++ | Turns the latest snapshot plus business data into a score and observations |
| Local data folder | `QStandardPaths::AppDataLocation` | Job specs, logs, screenshots, crawl output |
| Secrets | OS credential store (for example QtKeychain) | MySQL password and API keys |

**Decisions**

1. **PHP is not run by the desktop app.** Port only the logic the app still needs: `UrlNormalizer`, `SnapshotDiff`, `SiteQuery` filters, and the migrations.
2. **Move snapshot normalization into Python.** Today PHP `ProfileNormalizer` builds `summary_json` from the raw profile. `site_profile` should emit that summary itself, so C++ stores and displays it without re-implementing the mapping.
3. **Python does the network and parsing work.** Port a script to C++ only when a measurement shows it is the bottleneck. Most analysis time is network wait, which C++ will not shorten.
4. **Python never connects to MySQL.** Tools write a result file; the C++ job runner ingests it in one transaction. This keeps a single writer and a single place for credentials.
5. **No database call on the UI thread.** Each worker thread opens its own `QSqlDatabase` connection, and the data layer reconnects after a dropped connection. A remote database makes every query slow enough to freeze the UI otherwise.
6. **MySQL driver is the first risk to retire.** Official Qt builds often ship without the `QMYSQL` plugin, so it may need building against MariaDB Connector/C, or the data layer can use that connector directly. Prove the connection on the target machine in the first milestone.
7. **Files are local, records are remote.** The database stores paths relative to the local data folder. Screenshots and crawl output will not follow the user to a second machine (see Open questions).

## Site analysis: existing checks

Every section the current profiler produces stays, shown on the site detail page in the same order. The specification for each is `web-dashboard/docs/PRD.md` section 5.3 and `scripts/site_profile/schema_v2.json`.

| Section | Contents |
| --- | --- |
| Overview | HTTP status, final URL, CMS, hosting, server, SSL expiry, domain expiry, domain age, last fetched |
| Tech stack | Backend language, frontend framework, CMS with theme and plugins, JS libraries, analytics; version and evidence per item |
| Design age | Domain registration date, oldest WordPress upload, oldest and newest script/stylesheet dates, footer copyright year, oldest sitemap modification, earliest Wayback capture; one estimate with a confidence level |
| Hosting, DNS and email | IPs, ASN, reverse DNS, NS, MX with provider, TXT records, DNSSEC, CAA |
| SSL / TLS | Verified, issuer, validity dates, days left, SANs, cipher |
| Domain registration | Registrar, registrant, created, expires, statuses, transfer lock |
| SEO and meta | Title, description, canonical, robots, lang, hreflang, Open Graph, structured data, headings, images missing alt |
| Performance | TTFB, total time, HTML size, compression, redirect chain, cache headers |
| Pages and sitemap | robots.txt, sitemaps, URL list |
| Security headers and cookies | Present or missing checklist, cookie flags |
| Raw JSON | The full snapshot |

Snapshot history and diff, notes, to-dos, categories, tags, contacts, links, activity log, saved views, and the content crawl (`site_crawl`) also carry over.

**Hardening requirements**

- Each section reports its own status: `ok`, `failed`, `skipped`, or `timeout`. A failed section shows its error and a retry button; the other sections still render.
- A snapshot is stored for every attempt, including failed ones, so history shows what happened.
- The detail page fills in section by section as progress events arrive.
- Bot-protection pages, invalid TLS, non-HTML responses, and unreachable hosts each produce a labelled result, never an unhandled error.
- The hard limit for one profile job stays 120 seconds.

## Site analysis: new checks

Three new sections join the profile: email authentication, trackers, and contact channels. They ship as profiler schema v3, with the same rule as v2 that every key is always present and `null` when unknown.

### Email authentication and mail exchanger

The current profile already reports MX, the email provider, and SPF/DMARC as pass or missing. Extend it to show the raw values and a verdict.

| Item | How to get it | What to store and show |
| --- | --- | --- |
| MX | MX query on the registrable domain against a configurable resolver (default `8.8.8.8`, fallback `1.1.1.1`) | Every record with priority and host; inferred provider (Google Workspace, Microsoft 365, Yandex, Zoho, hosting mail) |
| SPF | TXT at the domain starting with `v=spf1` | Raw value; `all` qualifier (`-all`, `~all`, `?all`, `+all`); include list; DNS lookup count against the limit of 10; error when more than one SPF record exists |
| DMARC | TXT at `_dmarc.<domain>`, falling back to the organizational domain | Raw value such as `v=DMARC1; p=none; rua=mailto:info@company.com`; parsed `p`, `sp`, `pct`, `rua`, `ruf`, `adkim`, `aspf` |
| DKIM | TXT at `<selector>._domainkey.<domain>` for a list of common selectors | Selectors found, key type and length |

- **DKIM cannot be listed, only probed.** Try the selectors that match the detected provider first (`google` for Google Workspace, `selector1` and `selector2` for Microsoft 365), then a general list (`default`, `k1`, `s1`, `s2`, `mail`, `dkim`, `smtp`, `zoho`, `mandrill`, `key1`, `protonmail`). Keep the list editable in settings.
- When no selector answers, show "no common selector found", not "DKIM missing".
- **Verdict:** one badge per record (pass, weak, missing) and an overall label. `p=none` or a missing DMARC record counts as weak.
- **My sending domains:** the same check runs on the user's own domains from settings and shows a pass/fail panel before outreach starts.

### Ad and analytics trackers

The purpose is to learn whether the business pays to bring visitors to this site. Detect each tracker, its ID, its version, and whether that version is deprecated.

| Tracker | Evidence to look for | Deprecation rule |
| --- | --- | --- |
| Google Ads | `AW-` IDs in `gtag` config; `googleadservices.com/pagead/conversion` | Legacy `conversion.js` is deprecated |
| Google Analytics | `G-` IDs (GA4); `UA-` IDs, `analytics.js`, `ga.js` | Universal Analytics stopped processing data in July 2023, so a `UA-` tag alone means nobody is looking at the numbers |
| Google Tag Manager | `GTM-` container ID | Fetch the container script and scan it for the vendors below |
| Meta Pixel | `connect.facebook.net/.../fbevents.js`; `fbq('init', id)` | Flag pre-`fbevents` pixel code as legacy |
| TikTok Pixel | `analytics.tiktok.com`; `ttq.load` | None |
| LinkedIn Insight Tag | `snap.licdn.com`; `_linkedin_partner_id` | None |
| Microsoft Ads UET | `bat.bing.com` | None |
| Yandex Metrica | `mc.yandex.ru/metrika` | None |
| X, Pinterest, Snapchat, Criteo | Their pixel script hosts | None |
| Hotjar, Microsoft Clarity | Their script hosts | None; these show interest in conversion, not ad spend |

- **Two passes.** The static pass reads the HTML and inline scripts. The rendered pass loads the page in headless Chromium (`site_render`) and records network requests for about 8 seconds, because tags injected through Tag Manager do not appear in the HTML.
- **Rules live in a data file** (`trackers.yaml`: vendor, product, match patterns, ID pattern, status, note), so deprecations can be updated without a code change.
- **Per tracker, store:** vendor, product, ID, version, status (`current`, `deprecated`, `unknown`), evidence, and source (`static`, `gtm`, `rendered`).
- **Derived flags on the site:** `runs_ads`, `ad_platforms`, `stale_analytics`.
- **A pixel is not proof of an active campaign.** The detail page offers two buttons that open the Meta Ad Library and the Google Ads Transparency Center for the business in the browser, plus a manual "active ads confirmed" tick box.

### Contact channels

The current profile already collects emails, phones, address, socials, and WhatsApp from the homepage. Extend it so the app can say which channel to use.

- **Pages scanned:** the homepage plus up to five contact-like pages (`/iletisim`, `/bize-ulasin`, `/contact`, `/kontakt`, `/hakkimizda`, `/about`), found from links, not guessed.
- **WhatsApp:** `wa.me/`, `api.whatsapp.com/send`, `web.whatsapp.com/send`, and `whatsapp://send` links; chat widgets (Joinchat, Click to Chat, GetButton, Elfsight). Extract the number and any prefilled text.
- **Phones:** `tel:` links, schema.org `telephone`, and visible text. Normalize to E.164 with the `phonenumbers` library and classify each as mobile, fixed line, or other.
- **WhatsApp candidate:** a number from a WhatsApp link is `confirmed`; a mobile number without one is `likely`. Do not check numbers against WhatsApp by automation.
- **Emails:** classify as generic (`info@`, `iletisim@`) or personal; record the page each was found on.
- **People:** names and roles from team or about pages and schema.org `Person`, best effort.
- **Actions on the detail page:** "Open WhatsApp chat" builds `https://wa.me/<number>?text=<message>`; "Compose email" opens the outreach composer.

## Lead discovery

Discovery creates businesses in bulk from five kinds of source. Each run is a background job that records its query, its result count, and how many businesses were new.

| Source | Method | Priority |
| --- | --- | --- |
| Google Places | Text Search (New) with a niche and a place, for example "diş kliniği Kadıköy" | P0 |
| Single URL, pasted list, CSV | Direct import; CSV columns mapped in a dialog | P0 |
| Directories | Exporter union members, chambers of industry, industrial zone lists, fair exhibitor lists; one adapter per site, or a generic list scraper configured with CSS selectors | P1 |
| Web search API | Footer-phrase queries such as `"Tüm hakları saklıdır 2015" diş kliniği`; finding the website of a business known only by name | P1 |
| B2B listing cross-check | Marks a business that has a profile on a foreign-buyer marketplace | P2 |

**Google Places details**

- Request: `POST https://places.googleapis.com/v1/places:searchText` with the `X-Goog-Api-Key` and `X-Goog-FieldMask` headers. A request without a field mask fails.
- Fields needed: `places.id`, `displayName`, `formattedAddress`, `types`, `businessStatus`, `googleMapsUri`, `websiteUri`, `nationalPhoneNumber`, `internationalPhoneNumber`, `rating`, `userRatingCount`.
- The website, phone, and rating fields bill at the Enterprise SKU. Keep the field mask in one named constant.
- One query returns at most 20 results per page and 60 in total. To cover a city, the app splits a search into one query per district.
- Use `languageCode` and `regionCode` from the run's settings, and `locationBias` or `locationRestriction` when the user picks an area.
- A business with no `websiteUri` is still imported, flagged `no_website`.
- Store `place_id` permanently. Check the Places API policies on how long other fields may be kept, and refresh them on a schedule if required.

**Rules for every source**

- **Dedupe** on `place_id`, then on registrable domain, then on normalized phone. A duplicate updates the existing business and is counted as "already known".
- **Quota guard:** each paid API has a monthly budget in settings. The app counts calls, shows usage on the dashboard, and refuses to start a run that would pass the limit.
- **Saved searches** can be re-run; only new businesses are added.
- **Auto-analyze:** a run can queue a profile job for each new business with a website.
- **Politeness:** directory adapters respect robots.txt and send at most one request per second per host.

## Lead scoring

A score from 0 to 100 answers three questions: does the site need work, can the business pay, and can the owner be reached. The weights below are starting defaults and must be editable in settings.

| Group | Signal | Source | Points |
| --- | --- | --- | --- |
| Need (max 50) | No viewport meta tag | Profile | 12 |
|  | Design age 5 years or more (3–5 years: 6) | Design age section | 12 |
|  | No website at all | Places | 12 |
|  | Tagged as exporter, but no English pages or `hreflang` | Profile + tag | 10 |
|  | Mobile performance score under 50 | PageSpeed Insights | 8 |
|  | No HTTPS, or invalid certificate | TLS section | 8 |
|  | Outdated CMS or libraries | Tech stack | 5 |
|  | Missing title or meta description | SEO section | 4 |
| Ability to pay (max 35) | Ad pixel present | Trackers | 12 |
|  | 100 or more Google reviews (30–99: 5) | Places | 10 |
|  | Active ads confirmed by the user | Manual tick | 5 |
|  | Paid B2B listing | Cross-check | 5 |
|  | Social profiles linked | Contacts | 3 |
| Reachability (max 15) | WhatsApp link or mobile number | Contacts | 6 |
|  | Personal email (generic only: 2) | Contacts | 5 |
|  | Named decision-maker | Contacts | 4 |

- Each group is capped at its maximum, so the total never passes 100.
- **Tiers:** A is 70 or more, B is 50–69, C is under 50.
- **Disqualifiers** set the tier to `disqualified` whatever the score: business closed, site redesigned within two years, marked as a chain or franchise, already a client, or on the do-not-contact list.
- **Needs a manual look:** unreachable or bot-blocked sites get no score and a `review` flag.
- **Explainable:** store the breakdown (signal, points, evidence) as JSON and show it on the detail page.
- **Observations:** every Need signal has a one-sentence template in Turkish and English ("the menu does not open on mobile", "the browser shows 'not secure'"). The detail page lists the ones that apply; the user picks one for the outreach message and should verify it by hand.
- The score is recomputed after each new snapshot and whenever weights change.

## Outreach and pipeline

The app prepares every message and records every contact, but the user sends each one by hand. This keeps a new sending domain safe and keeps each message personal.

**Pipeline**

- Statuses: `new`, `analyzed`, `qualified`, `concept ready`, `contacted`, `follow-up due`, `in conversation`, `won`, `lost`, `rejected`, `do not contact`.
- Two views of the same data: a table with filters (tier, score, niche, city, status, tags, has WhatsApp, runs ads) and a board with one column per status.
- Bulk actions: change status, tag, queue analysis, reject.

**Templates and composer**

- Templates are stored per channel (email, WhatsApp), language (Turkish, English, more can be added), and step (first touch, follow-up 1 to 3).
- Variables: `{{company}}`, `{{contact_name}}`, `{{observation}}`, `{{preview_url}}`, `{{niche}}`, `{{my_name}}`, `{{my_phone}}`, `{{my_site}}`.
- The composer fills a template for one business and shows the result for editing. Buttons: copy, open in the default mail client, open WhatsApp chat.
- The first-touch email template carries no preview link by default; the link goes into the reply template.
- "Mark as sent" stores the final text, channel, and time, and moves the business to `contacted`.

**Follow-ups**

- Marking a first touch as sent schedules follow-ups at 3, 7, and 14 days (editable).
- The dashboard lists follow-ups due today. Marking a reply cancels the remaining ones and moves the business to `in conversation`.

**Safeguards**

- **Daily cap:** a counter of emails marked sent today against a limit from settings (default 20), with a warning at the limit.
- **Do-not-contact list:** domains, emails, and phone numbers. An opt-out adds an entry, and the composer refuses to open for a listed business.
- **Duplicate guard:** a warning when the same email address or number was contacted in the last 30 days.

**Concept previews**

- Each business has a `preview_url` field for the redesign concept (the current schema already has this on sites).
- **Visit tracking:** show "preview visited" events with time on the business timeline, and raise the business in the follow-up list. How visits reach the app is an open question.
- **Before/after image (P2):** combine the stored screenshot of the current site with a screenshot of the preview into one image about 600 px wide and under 200 KB, ready to paste into an email.
- **Drafted observation (P2):** send the score breakdown to a language model and get a first-touch draft back. The user always edits before sending.

## Gmail integration

The app connects to the user's mailbox read-only and shows, on each business, every email sent to or received from that company. It also replaces the manual "mark as sent" and "mark as replied" steps with automatic detection. It ships with the outreach milestone (M6).

**Access**

- Scope: `https://www.googleapis.com/auth/gmail.readonly` only. The app never sends, deletes, labels, or changes mail.
- Sign-in: OAuth 2.0 for desktop apps, using the system browser, a loopback redirect on `127.0.0.1`, and PKCE. Qt's Network Authorization module covers this flow.
- Google classes this scope as [restricted](https://support.google.com/cloud/answer/13464325). An OAuth app marked [internal-only](https://support.google.com/cloud/answer/13463817) to a Google Workspace organization does not need Google's verification, so create the OAuth client in the Workspace account's Google Cloud project with the consent screen set to Internal.
- The refresh token is kept in the OS credential store. Settings has Connect, Disconnect (which also revokes the token), and a status row on the health page.
- The Gmail client is written in C++ with Qt Network, so tokens never leave the app process.

**Matching mail to a business**

1. Exact match on any email address stored in the business's contacts, in From, To, or Cc.
2. Match on the registrable domain of the business's sites, for example any address at `company.com.tr`.
3. Free-mail domains (gmail.com, hotmail.com, outlook.com, yahoo.com, yandex.com, icloud.com) never match by domain, only by exact address.
4. The user can link or unlink a thread by hand; a manual choice is never overwritten by sync.

**Sync**

- Opening a business runs a search for its addresses and domain (`users.messages.list` with a `from:` / `to:` / `cc:` query) and loads metadata for the results.
- While the app is open, a background sync every 5 minutes reads changes since the stored `historyId` (`users.history.list`) and matches new messages against all businesses. If the history ID has expired, fall back to a search of the last 30 days.
- Store headers and the snippet in MySQL. Fetch a message body only when the user opens it, and do not store bodies in the remote database.
- Back off and retry on rate-limit responses; batch metadata requests.

**Display**

- An Emails tab on the business detail page: threads newest first, each with direction, correspondent, subject, snippet, date, and an attachment marker.
- Opening a thread shows the messages as plain text. HTML bodies are sanitized and remote images stay blocked, so reading a message in the app never triggers a tracking pixel.
- "Open in Gmail" opens the thread in the browser, where the user replies.
- Sent and received emails appear on the business timeline.

**Automation**

- **Sent detected:** a sent message to a matched address creates or completes the contact-log entry, moves the business to `contacted`, and schedules follow-ups. A message composed in the app is paired with it by recipient and subject.
- **Reply detected:** an inbound message from a matched address marks the entry `replied`, cancels remaining follow-ups, moves the business to `in conversation`, and shows on the dashboard.
- **Bounce detected:** a delivery-failure notice naming a contacted address marks the entry `bounced`, flags the contact as invalid, and warns the user, because bounces harm a new sending domain.
- **Auto-replies** (out-of-office, `Auto-Submitted` header) are shown but do not count as a reply.
- The "Sent today" widget counts real sent messages to lead addresses instead of manual marks.

**To confirm**

- Which mailbox or mailboxes should connect: only the Workspace address used for outreach, or the personal Gmail address as well? A personal account cannot use an internal-only OAuth app.
- **Later option (P2):** a "Create Gmail draft" button that puts the composed message into Gmail's drafts for the user to send. It needs the `gmail.compose` scope, which is also restricted.

## Freelance tools and dashboard

A won lead becomes a client, and the same business record then carries its projects, sites, and history.

- **Clients and projects:** a project has a name, status, start date, deadline, and linked sites. Notes and to-dos can attach to a business, a site, or a project.
- **Notes:** Markdown, with search across all notes.
- **To-dos:** due date, priority, done state; a global list and a per-record list.
- **Expiry watch:** domain and SSL expiry for every client site, re-checked on a schedule, with thresholds at 30 and 14 days.
- **Re-fetch schedule:** client sites are re-profiled weekly so changes show up in the snapshot diff.
- **Content crawl:** `site_crawl` downloads a client's existing pages, images, and files as migration material.
- **Activity log:** every job, status change, note, and message appears on the record's timeline.

**Dashboard widgets**

| Widget | Shows | Status |
| --- | --- | --- |
| Projects by status | Count per status | Existing |
| Upcoming deadlines | Projects and to-dos due soon | Existing |
| Expiring soon | Domains and certificates near expiry | Existing |
| Recently fetched | Latest profile snapshots | Existing |
| Open to-dos | Count and the next few | Existing |
| Background jobs | Running and queued jobs with progress | Existing |
| Lead funnel | Businesses per pipeline status | New |
| Follow-ups due | Today's follow-ups with a compose button | New |
| Sent today | Emails marked sent against the daily cap | New |
| Top new leads | Highest-scoring A-tier businesses not yet reviewed | New |
| API usage | Calls and estimated cost per paid service this month | New |

**Settings and health**

- Settings: database connection, API keys, resolver, DKIM selector list, score weights, follow-up intervals, daily cap, sender identity, data folder.
- Health page: database reachable, migrations current, Python and the uv environment found, Chromium for `site_render` installed, each API key tested with one cheap call, data folder writable. Every row has a fix hint.

## Data model

The database starts empty: no data is migrated from the web dashboard. The MySQL schema reuses only the table designs of the current schema, converted to MySQL types, plus a `businesses` table as the new central record and the tables below. A business has zero or more sites, because a Places result may have no website.

**Table designs reused from `backend/migrations/0001_init.sql` and `0002_preview_url.sql`:** `schema_migrations`, `categories`, `tags`, `sites`, `site_tags`, `profile_snapshots`, `snapshot_technologies`, `notes`, `todos`, `contacts`, `site_links`, `activity`, `jobs`, `crawls`, `saved_views`, `settings`.

**Changes to reused tables**

- `sites` gains `business_id` (nullable) and denormalized filter columns updated on ingest: `runs_ads`, `dmarc_policy`, `has_whatsapp`, `mobile_score`, `design_year`.
- `notes`, `todos`, `contacts`, and `activity` gain nullable `business_id` and `project_id`, so a record can attach to a business, a site, or a project.
- `contacts` gains `kind` (generic or personal email, mobile, fixed line), `whatsapp_state` (`confirmed`, `likely`, `none`), `source_url`, and `role`.

**New tables**

| Table | Purpose | Key columns |
| --- | --- | --- |
| `businesses` | The lead or client | `name`, `place_id` (unique), `niche`, `city`, `country`, `address`, `phone_e164`, `rating`, `review_count`, `business_status`, `pipeline_status`, `score`, `tier`, `score_breakdown` (JSON), `is_client`, `preview_url`, `ads_confirmed`, `is_chain` |
| `discovery_runs` | One discovery job | `source`, `query`, `location`, `params` (JSON), `job_id`, `result_count`, `new_count`, `api_units` |
| `discovery_results` | Which run found which business | `run_id`, `business_id`, `raw` (JSON) |
| `snapshot_trackers` | Trackers found in a snapshot | `snapshot_id`, `vendor`, `product`, `tracker_id`, `version`, `status`, `source`, `evidence` |
| `screenshots` | Captured images | `site_id`, `snapshot_id`, `kind` (desktop, mobile, full page), `path`, `width`, `height` |
| `message_templates` | Outreach templates | `name`, `channel`, `language`, `step`, `subject`, `body` |
| `outreach_messages` | Contact log | `business_id`, `contact_id`, `channel`, `template_id`, `subject`, `body`, `status`, `sent_at`, `replied_at` |
| `followups` | Scheduled follow-ups | `business_id`, `step`, `due_at`, `status` |
| `suppressions` | Do-not-contact list | `kind` (domain, email, phone), `value`, `reason` |
| `projects` | Client work | `business_id`, `name`, `status`, `starts_on`, `deadline` |
| `project_sites` | Sites in a project | `project_id`, `site_id` |
| `preview_visits` | Visits to concept previews | `business_id`, `url`, `visited_at` |
| `api_usage` | Quota guard counters | `service`, `period`, `units` |
| mail\_accounts | Connected Gmail mailboxes | email, history\_id, last\_sync\_at, status |
| mail\_messages | Metadata of emails matched to a business | account\_id, gmail\_id (unique per account), thread\_id, business\_id, contact\_id, direction, from\_addr, to\_addrs, subject, snippet, sent\_at, is\_bounce, is\_auto\_reply, linked\_manually |

**Rules**

- InnoDB, `utf8mb4`, all times stored in UTC as `DATETIME(3)`, JSON in native `JSON` columns.
- Email authentication results stay inside the snapshot JSON; only `dmarc_policy` is copied out for filtering.
- Migrations are numbered SQL files applied by the app at start-up and recorded in `schema_migrations`. A failed migration stops start-up with a clear message.

## Python tools and job runner

The C++ job runner keeps the contract the PHP worker uses today (`web-dashboard/docs/PRD.md` section 9), so the existing tools run unchanged.

**Contract**

- Invocation: `<venv python> -m <tool> --spec <data>/jobs/<id>/spec.json`. Inputs travel in the spec file, never in arguments.
- stdout carries only NDJSON events, one per line: `progress` (`current`, `total`, `label`), `stats`, `log` (`level`, `message`), `result` (`path`).
- stderr is free text and goes to `<data>/jobs/<id>/job.log`.
- Exit codes: `0` ok, `1` partial, `2` invalid input, `3` unreachable, `4` blocked by network policy, `130` cancelled, anything else is a crash.
- The private-network guard stays on unless the spec sets `allowPrivate`.

**Tools**

| Tool | Status | Job type | Output |
| --- | --- | --- | --- |
| `site_profile` | Existing, extended to schema v3 | `profile` | Raw profile plus the normalized summary; new `email_auth`, `trackers`, and extended `contacts` sections |
| `site_crawl` | Existing | `crawl` | Pages as Markdown, images, files, `structure.json` |
| `site_render` | New (Playwright, headless Chromium) | `render` | Desktop (1440×900) and mobile (390×844) screenshots, a full-page capture, and the network request log for the rendered tracker pass |
| `site_speed` | New | `speed` | Mobile performance score and core metrics from the PageSpeed Insights API |
| `lead_discover` | New | `discover` | A list of businesses from Places, a CSV, a directory adapter, or a search API |

New Python dependencies: `playwright`, `phonenumbers`, `pyyaml`.

**Job runner requirements**

- The `jobs` table is the queue. Limits: 3 `profile`, 2 `render`, 2 `speed`, 1 `crawl`, 1 `discover` at a time (editable).
- Start each child with `QProcess` using a program and an argument list, never a shell string. Set `PYTHONUTF8=1` and `PYTHONUNBUFFERED=1`.
- Parse stdout line by line. A line that is not valid JSON is written to the log and skipped.
- Timeouts: profile 120 s, render 90 s, speed 90 s, discover 10 min, crawl 2 h by default.
- Cancel: write `<data>/jobs/<id>/cancel` (the tools poll for it), wait 5 seconds, then kill the process tree.
- Record a heartbeat every 2 seconds. At start-up, mark `running` jobs with no live process as `failed` and offer a retry.
- On exit, ingest the result file in one database transaction, write an activity entry, and recompute the score.
- Closing the app with jobs running asks whether to cancel them or wait.
- A chained "full analysis" for one site runs `profile`, then `render` and `speed` in parallel, then scoring.

## External services and API keys

Two keys are required for v1: Google Places and PageSpeed Insights, both from one Google Cloud project. Everything else is optional or needs no key.

| Service | Used for | Key | Notes |
| --- | --- | --- | --- |
| [Places API (New), Text Search](https://developers.google.com/maps/documentation/places/web-service/text-search) | Lead discovery | Required | Needs a billing account. The fields this app requests bill at the Enterprise SKU; check the current price list and set a monthly budget |
| PageSpeed Insights API | Mobile performance score | Required | Enable it in the same Google Cloud project |
| Web search API | Footer-phrase searches, finding a site from a business name | Optional (P1) | Choose a provider that sells general web results, for example Brave Search API, and check its current plans |
| [Google Custom Search JSON API](https://developers.google.com/custom-search/v1/overview) | Not usable | — | Closed to new customers; existing customers have until January 1, 2027 |
| DuckDuckGo | Not usable | — | No official API for general web results, as far as I know; not checked for this document |
| Meta Ad Library, Google Ads Transparency Center | Confirming active ads | None | Opened in the browser from the detail page; no API call |
| RDAP, WHOIS, Wayback Machine, public DNS | Existing profile sections | None | Already in `site_profile` |
| Language model API | Drafted observations (P2) | Optional | Any provider; off by default |
| Remote MySQL | All records | Host, port, schema, user, password | Supplied by the user at first run |
| Gmail API | Emails exchanged with each business; sent, reply, and bounce detection | Optional: OAuth client ID (Desktop type), not an API key | Read-only scope; consent screen set to Internal in the Workspace organization. See Gmail integration |

- Keys and the database password are stored in the OS credential store.
- The settings page has a "Test" button per key.

## Non-functional requirements

**Stability**

- No input from the network, a child process, or the database may crash the app. Every parser treats its input as untrusted and returns an error value instead of throwing past its module.
- A crashed or hung Python process fails only its own job.
- Every error shown to the user carries a reference that can be found in the log. Logs rotate and are kept 14 days.
- Acceptance test: 50 analyses in a row, including an unreachable host, a bot-protection page, an invalid certificate, a non-HTML response, and a 10 MB page, finish without a crash or a frozen window.

**Performance**

- The UI thread never waits on the database, the network, or a process.
- Single URL: first sections visible within 5 seconds of the first progress event; full analysis usually under 60 seconds.
- Bulk: 100 businesses analyzed in the background while the app stays responsive.
- Lists page their queries and stay smooth at 10,000 rows.

**Security**

- MySQL over TLS, with a dedicated user limited to one schema. Restrict the user by IP address if the host allows it.
- All SQL uses bound parameters.
- Crawled and fetched content is displayed as text or sandboxed, never executed.
- Child processes are started without a shell, with fixed program paths.

**Compliance**

- Google Maps data comes only from the Places API, within its storage rules.
- Directory adapters respect robots.txt and rate limits.
- Only business contact details published on a company's own pages or in Places are stored.
- The app never sends messages itself, and the do-not-contact list is enforced in the composer.
- Whether a given outreach is lawful under KVKK and the İYS rules is the user's responsibility; the app provides the opt-out record.

**Testing**

- Python: pytest with offline fixtures, as today, plus fixtures for trackers, email authentication, WhatsApp links, and phone formats.
- C++: unit tests for scoring, URL normalization, snapshot diff, migrations, and the job runner against a fake job script.
- Each milestone ends with a manual checklist run on the target machine.

## Milestones

Each milestone is usable on its own, and none starts before the previous one passes its check.

| Milestone | Scope | Done when |
| --- | --- | --- |
| M0 Foundation | MySQL connection from Qt, migrations, credential storage, job runner, health page | One URL entered in a bare screen runs `site_profile` and shows live progress; the health page is all green |
| M1 Parity | Sites list and detail with every existing section, snapshots and diff, notes, to-dos, tags, categories, activity, existing dashboard widgets | Everything the web dashboard does today works in the Qt app, and the stability test passes |
| M2 New checks | Email authentication, static and Tag Manager tracker detection, extended contact channels, schema v3 | A site with a `p=none` DMARC record, a Meta Pixel, and a WhatsApp link shows all three correctly |
| M3 Render and speed | `site_render` screenshots, rendered tracker pass, `site_speed` | Desktop and mobile screenshots appear on the detail page; a tag loaded only through Tag Manager is detected |
| M4 Discovery | `businesses` table, Places search, URL and CSV import, dedupe, quota guard | "diş kliniği Kadıköy" creates businesses without duplicates on a second run and stops at the budget |
| M5 Scoring and pipeline | Scoring engine, tiers, observations, pipeline table and board, filters | Leads sort by score, and each score shows its breakdown |
| M6 Outreach | Templates, composer, contact log, follow-ups, do-not-contact list, daily cap, preview visits, Gmail sync | A first touch can be composed, marked sent, and followed up from the dashboard |
| M7 Clients and polish | Projects, expiry watch, scheduled re-fetch, directory and search adapters, installer | A won lead becomes a client with a project; a fresh machine installs and passes the health page |

## Open questions

The coding agent should ask these before M0, because each one changes the build.

- [ ] Is the Qt template built with Qt Quick (QML) or Qt Widgets, and on which Qt version?
- [ ] Which operating systems must the app run on: Windows only, or macOS as well?
- [ ] Will the app run on one machine or several? If several, screenshots and crawl output need shared storage.
- [ ] How are preview-link visits recorded today (which database and table), and can the app read them from the same MySQL server?
- [ ] Does the MySQL host offer TLS, and does it allow remote connections from a changing IP address?
- [ ] What is the monthly budget for the Places API?
- [ ] Which languages do templates need at launch besides Turkish and English?
- [ ] Is the content crawl needed in v1, or can it wait for M7?
- [ ] Should drafted observations use a language model, and from which provider?

## Sources

- [freelance-dashboard repository](https://github.com/HyperGrapher/freelance-dashboard): `README.md`, `docs/PRD.md` (sections 5.2–5.4 and 9), `backend/migrations`, `scripts/site_profile/schema_v2.json`, `scripts/pyproject.toml`
- [Text Search (New), Google Maps Platform](https://developers.google.com/maps/documentation/places/web-service/text-search): request format, field mask, SKUs, 20 per page and 60 total
- [Custom Search JSON API overview](https://developers.google.com/custom-search/v1/overview): closed to new customers, last updated 2026-02-18

Score weights, timeouts for new job types, and the DKIM selector list are proposed defaults, not measured values. Tracker deprecation notes and the Qt MySQL driver note come from general knowledge and should be confirmed against current documentation during the build.
