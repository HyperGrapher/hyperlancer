# Freelance Dashboard

A local dashboard for managing client websites: status, notes, to-dos, site profiles (stack, DNS, SSL, SEO), content crawls and background jobs. React + Vite frontend, PHP + SQLite backend, Python tools for profiling and crawling.

Status: milestones M0–M5 are built (sites, profiles and history, notes, to-dos, activity, contacts and links, dashboard, jobs, settings and content crawls). M6 (polish and hardening) is in progress. See [`docs/HANDOFF.md`](docs/HANDOFF.md) for where the project stands, [`docs/PRD.md`](docs/PRD.md) for the spec and [`docs/IMPLEMENTATION_PLAN.md`](docs/IMPLEMENTATION_PLAN.md) for milestones.

## Requirements

- Node.js 20.19+ and pnpm
- PHP ≥ 8.1 with `pdo_sqlite`, `intl`, `zip`, `mbstring`, `fileinfo`, `openssl`, and the **CLI** binary (`php` / `php.exe`) next to the web PHP
- Python ≥ 3.11 and [`uv`](https://docs.astral.sh/uv/) for the profiler and crawler
- Composer, only to run the PHP tests (the backend has no runtime Composer packages)

## Linux / development

```bash
pnpm install
uv sync --project scripts
pnpm dev:api      # php -S 127.0.0.1:8787 (APP_ENV=development)
pnpm dev          # Vite on :5173, proxies /api to http://127.0.0.1:8787 (override with PHP_API_TARGET)
pnpm dev:worker   # optional: the job worker as a foreground daemon with live logs
```

Without `pnpm dev:worker`, the API starts a worker on demand whenever a job is queued; it exits after 10 idle seconds.

Tests and checks:

```bash
(cd backend && composer install)   # once, for PHPUnit
pnpm lint && pnpm typecheck && pnpm test && pnpm build
```

`pnpm test` runs Vitest, PHPUnit and pytest.

## Windows with Appytizer

1. Install Python ≥ 3.11 and `uv`, then run `uv sync --project scripts` in the project folder. This creates `scripts\.venv`; PHP calls its `python.exe` by absolute path.
2. Enable `pdo_sqlite`, `intl`, `zip`, `mbstring`, `fileinfo` and `openssl` in Appytizer's `php.ini`.
3. Run `pnpm install && pnpm build`. `dist` is served at `http://dashboard.test` through the junction.
4. Open Settings → Health. Everything should be green. Use **Run test** under "Worker spawn test": a 3-minute background job that must keep running and can be canceled.
   - If it dies when php-cgi recycles, run the worker as a daemon instead: register `php.exe backend\bin\worker.php --daemon` in Task Scheduler "At log on". The launcher sees its lock and will not spawn a second worker.
5. If deep links such as `/sites/1` return 404, the server does not fall back to `index.php`. Build with `VITE_ROUTER_MODE=hash` to use `/#/sites/1` URLs.

## How it is served

`pnpm build` writes `dist/` with the SPA, a front controller `dist/index.php` (`/api/*` goes to PHP, any other extension-less path returns `index.html`), and a copy of `backend/` without tests, vendor, data and local config. It also writes `dist/backend/build-info.php` with the absolute project root, so PHP finds the project even when `dist` is reached through a junction.

## Where the data lives

`<project>/data` (gitignored), never inside `dist/`:

```
data/app.sqlite   database (WAL)       data/backups/   automatic and manual backups
data/jobs/<id>/   spec, log, events    data/logs/      app and worker logs (14 days)
data/run/         worker.lock          data/crawls/    crawl output, one folder per run
data/archives/    cached crawl ZIPs    data/favicons/  cached site icons
```

`pnpm build` never touches it, and the build aborts if `APP_DATA_DIR` points inside `dist/`. At runtime the backend also refuses to start with a data dir inside the build output.

Override per machine with environment variables (`APP_DATA_DIR`, `APP_PYTHON`, `APP_PHP_CLI`, `APP_ALLOWED_HOSTS`, `APP_ENV`) or a `config.local.php` copied from `backend/config/config.example.php`.

Migrations run automatically on the first request after a build, with a backup first. Manual commands: `pnpm migrate`, `php backend/bin/migrate.php --status`.

Upgrading from the old notes demo: its database in `backend/data/` or `dist/backend/data/` is ignored. Health shows a "Legacy data" row; `php backend/bin/migrate.php --import-legacy` moves it to `data/legacy/`.

## Upgrade notes

- Pull, run `uv sync --project scripts` and `pnpm install && pnpm build`. Migrations run on the next request, after an automatic backup in `data/backups/`.
- `generic_crawler` no longer exists; the Content tab uses `site_crawl`. Old crawl folders from the command line are not imported.
- Importing old `!Reports` profile JSONs is not part of the MVP (planned for v1.1). Re-fetch a profile instead; `php backend/bin/seed.php --demo` loads four sample profiles from `scripts/examples/`.

## Backup, restore and troubleshooting

- **Backup:** Settings → Backups → *Back up now*, then *Download*. A backup is a consistent copy of `app.sqlite`. The same folder (`data/backups/`) also gets one before every migration.
- **Restore:** stop the web server, copy the backup over `data/app.sqlite` (delete `app.sqlite-wal` and `-shm` next to it), start again.
- **Crawl output** lives in `data/crawls/` and is not part of the database backup; copy that folder if you want to keep it.
- **Something does not work:** open Settings → Health. It checks PHP extensions, the PHP CLI and Python paths, the data folder, migrations and the worker. Logs are in `data/logs/` (`app.log`, `worker.log`) and per job in `data/jobs/<id>/job.log`; every 500 error shows a `logRef` you can search for in `app.log`.
- **Security headers:** the front controller sends a Content-Security-Policy (scripts from this origin only), `X-Frame-Options: DENY`, `nosniff` and `Referrer-Policy: no-referrer`. Files from a crawl are served with a sandbox CSP and download as attachments unless they are images, PDFs or plain text.

## Python tools

`scripts/` is one uv project (`pyproject.toml`, `uv.lock`) for both tools. They are internal to the dashboard: the PHP worker runs them from a job spec, so they are not general-purpose CLIs.

```bash
cd scripts
uv run python -m site_profile example.com --output /tmp/profile.json   # profiler v2 (schema v2)
uv run python -m site_profile example.com                              # same, report on stdout
uv run python -m site_profile --spec data/jobs/12/spec.json            # job mode: NDJSON events on stdout
uv run python -m site_crawl https://books.toscrape.com/ --max-pages 3 --out /tmp/crawl   # crawler v2 (pages/*.md, images/, files/, structure.json)
uv run python -m site_crawl --spec data/jobs/12/spec.json              # job mode (the Content tab)
uv run pytest                                                          # offline tests
```

Both tools use the same exit codes: `0` ok, `1` partial, `2` invalid input, `3` unreachable, `4` blocked by the network policy, `130` cancelled. Private, loopback and LAN targets are refused unless `--allow-private` is given (or `allowPrivate` in the spec).

## API

Base path `/api`. Mutations need `Content-Type: application/json` and the header `X-Requested-With: freelance-dashboard`; the `Host` must be in the allow-list. Implemented so far (see `backend/src/App.php` for the full route table): sites, snapshots, categories, tags, saved views, bulk actions, plus `GET /api/health`, `GET|POST /api/backups`, `POST /api/system/spawn-test`, `GET /api/jobs`, `GET /api/jobs/:id`, `GET /api/jobs/:id/log`, `POST /api/jobs/:id/cancel`, `POST /api/jobs/:id/retry`. The full design is in PRD §8.
