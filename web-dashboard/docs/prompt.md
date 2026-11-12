# Task: Plan (PRD first), then build a local "Freelancer Dashboard" app

## Your role
You are a senior full-stack engineer and product designer. Before writing any app code, explore the project files, then produce a complete PRD and technical design. Make decisions yourself where I say "you decide", and justify each one briefly.

## Step 0: Explore first (do this before anything else)
1. Inspect the whole repository: structure, `package.json`, existing config, and especially:
   - `scripts/site_profile.py`: fetches a website profile, prints to CLI, and saves JSON to disk.
   - `scripts/generic_crawler/`: crawls a site and downloads text, images and files. Its output is sometimes chaotic and needs improvement.
2. Run both scripts against a sample site if possible. Study their exact output shape (JSON keys, folder layout, failure modes, dependencies, runtime).
3. Summarize what you found (data fields, weaknesses, missing information), then continue with the steps below.

## Product summary
I'm a freelance web developer. I need a personal dashboard to manage the websites/clients I work on. It should let me:
- Fetch, store and nicely view technical info about a client's current website (a "site profile").
- Track project status, categorize and tag projects.
- Keep notes (Markdown) per project.
- Track to-dos (open/done) per project.
- Download a site's content (text, images, files) in a structured way for use during development.

## Stack and constraints
- Frontend: React (SPA, built with Vite or similar, using `pnpm`)
- Backend: PHP (no framework required unless you can justify one; a light router is fine)
- Database: SQLite (single file)
- Runs **locally on my Windows PC only**. It is never deployed to a server, so no multi-user auth is needed. Still follow sane security basics (see below).

### How it is served locally
- `pnpm build` outputs the React `dist/` folder and **also copies the `backend/` folder into `dist/`**.
- `dist/` is junction-linked to `C:\appytizer\dashboard`.
- "Appytizer" is a Laravel Valet-like app: every folder under `C:\appytizer\` is served as `<foldername>.test` (Nginx + php-cgi). So the app lives at `http://dashboard.test`.
- During development you (the agent) run in a Linux environment. Make paths/config environment-aware (Windows vs Linux detection, config file or env vars) so the same code works in both. Document how to run it in each environment.
- Pay attention to php-cgi limitations: request timeouts, no long-running processes, Windows paths, and whether Python is available on PATH on my machine.
- Make sure `pnpm build` never overwrites or deletes my database or downloaded data.

## Feature 1: Site Profile (fetch and view)
**Flow:** I enter a website URL (and optionally category and status), the app fetches a profile, stores it, and shows it in a detail view.

Currently python script outputs json and I only see raw JSON. The UX must be much better:
- Design a clear, scannable detail view from the actual script output. Group the data into sections (for example overview, CMS/backend and tech stack, hosting/DNS/SSL, SEO/meta, performance, pages/sitemap, contact info, social links). Use cards, badges, icons, tables, collapsible sections, copy buttons, and links. Keep a "raw JSON" tab as a fallback.
- Handle failed or partial fetches gracefully (errors, timeouts, blocked sites).
- Keep **history**: re-fetching creates a new snapshot, and I can compare snapshots (what changed since the last fetch).

**Architecture decision (you decide):** rewrite the profiler in PHP, or keep/modify the Python script and call it from PHP (`proc_open`, background job, etc.)? Evaluate honestly: reliability on Windows, dependency burden, speed, maintainability, php-cgi timeouts, and the fact that I only run this locally. Pick one, explain the trade-offs, and note any changes needed to the existing script (for example a stable JSON schema, exit codes, a progress output).

**Storage decision (you decide):** store the profile JSON in SQLite (as JSON column and/or normalized fields for filterable data), as files on disk, or a hybrid. The list view needs fast filtering, so filterable fields should be real columns.

### Website list view
Columns at minimum:
- Domain
- Backend / CMS
- Fetch date
- Category (assigned at fetch time or later)
- Status: `none`, `design`, `waiting`, `done`, `canceled` 

The list needs strong filtering and sorting: by status, category, tags, CMS, date range, text search, and combinations of these. Make filters shareable via the URL query string. Also include saved filters/views, bulk actions (change status, add tag), and a table/card layout toggle if it adds value.

### Website detail view
- Site profile display (see above)
- Edit category, status, and tags
- **Notes:** multiple note items, each with a title, Markdown input (with live preview) and rendered Markdown display. Sanitize the rendered HTML.
- **To-dos:** checklist with open/done, optional due date and priority, plus an overview of open to-dos across all projects.
- Activity/history log (status changes, fetches, notes)

## Feature 2: Content Download (text, images, files)
Used rarely, but important when I start building a new site. I use the output for the new site's copy and images, or hand it to my coding agent to understand the site's content and structure.

- Review `scripts/generic_crawler/` and fix its chaotic output. Design a clean, predictable output structure, for example per domain: `pages/<slug>.md` (clean main content as Markdown), `images/`, `files/`, a `sitemap.json` / `structure.json` (page tree, URLs, titles, headings, internal links, which images belong to which page), and a short `README`/manifest that explains the layout so an AI agent can consume it directly.
- Options for the crawl: max depth/pages, same-domain only, include/exclude patterns, respect robots.txt (configurable), rate limiting, file-type filters, de-duplication of assets.
- Because php-cgi has timeouts, a crawl must run as a **background job** with a status/progress UI (queued, running, done, failed), logs, and the ability to cancel. Explain how you implement this on Windows and Linux.
- Same decision as above: keep it as an external Python script, or reimplement it in PHP. You decide, and justify it.
- In the UI: start a crawl from the website detail view, see progress, browse the downloaded results (pages, images gallery, files), download as ZIP, and open the folder location.

## Additional features (you decide what to include)
I'm probably missing things a freelancer needs. Propose and prioritize your additions as **MVP / v1.1 / later**. 
Some additional features:
- Client/contact info per project (name, email, phone, preferred channel) and a link to the production site, staging site, repo, hosting and registrar
- Credentials/access checklist (without storing plain-text passwords; or store references only, or encrypt locally; recommend an approach)
- Domain, SSL and hosting expiry tracking with reminders
- Offer amount, invoice status, payment tracking
- Project timeline/milestones, deadlines, and a dashboard home (active projects, upcoming deadlines, open to-dos, recently fetched sites)
- Kanban board view by status
- Uptime/health re-check and scheduled re-fetch of profiles
- Lighthouse/PageSpeed, broken link checker, accessibility and SEO quick audit
- Attachments (screenshots, briefs, contracts), and screenshots of the site during fetch
- Reusable templates (to-do checklists per project type, note templates)
- Global search across sites, notes and to-dos
- Export/backup (SQLite backup, JSON/Markdown export) and import
- Keyboard shortcuts, dark mode, command palette

## Non-functional requirements
- Good, modern, clean UX: dark theme, responsive, fast, consistent components, empty/loading/error states
- Validate and normalize URLs (domain extraction, duplicates detection: warn if the domain already exists)
- Basic security even though it's local: prepared statements, output escaping/sanitizing Markdown, input validation, CSRF considerations, safe handling of file paths and shell arguments (no command injection when calling Python), and SSRF awareness for the fetcher
- Database migrations (a simple versioned migration system) and seed/demo data
- Clear error messages and logs I can read
- Keep dependencies reasonable

## Deliverables, in this order
1. **Findings summary** of the existing scripts and repo (short).
2. **`docs/PRD.md`** containing: goals and non-goals, user stories, feature list with priorities (MVP / later), UX/screens and key flows (describe each screen, with ASCII wireframes if helpful), data model (ER overview and SQLite schema), API design (endpoints, request/response shapes), the background job design, file/folder structure for frontend, backend, scripts and data, and the environment/path strategy for Windows vs Linux.
3. **`docs/DECISIONS.md`**: each "you decide" item (Python vs PHP for profiler and crawler, storage approach, job runner, UI library, Markdown library, credentials handling), with options, choice and reasoning.
4. **Implementation plan**: milestones with a task checklist.
5. **Stop and ask** me to review the PRD and answer open questions before implementing. List open questions at the end of the PRD (keep them few and only those you can't decide yourself).
6. After my approval: implement milestone by milestone, with working builds, a short README (setup, dev, build, how it's served via Appytizer), and tests for the critical parts (parsers, API, job runner).