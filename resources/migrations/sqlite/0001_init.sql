-- Hyperlancer schema v1 (SQLite). Keep in step with mysql/0001_init.sql: same tables,
-- columns and meaning. Times are UTC text "yyyy-MM-dd HH:mm:ss.zzz", written by the app.

CREATE TABLE IF NOT EXISTS schema_migrations (
  version     INTEGER PRIMARY KEY,
  name        TEXT NOT NULL,
  applied_at  TEXT NOT NULL
);

CREATE TABLE sites (
  id                  INTEGER PRIMARY KEY,
  site_key            TEXT NOT NULL UNIQUE,          -- host without www., punycode
  registrable_domain  TEXT,                          -- eTLD+1 from the Public Suffix List
  url                 TEXT NOT NULL,                 -- normalized URL
  name                TEXT,
  source              TEXT NOT NULL DEFAULT 'manual' CHECK (source IN ('manual','discovery','import')),
  allow_private_net   INTEGER NOT NULL DEFAULT 0,
  preview_url         TEXT,
  -- Copied from the latest snapshot for lists and filters.
  latest_snapshot_id  INTEGER REFERENCES profile_snapshots(id) ON DELETE SET NULL,
  last_fetched_at     TEXT,
  last_fetch_outcome  TEXT CHECK (last_fetch_outcome IN ('ok','partial','failed')),
  http_status         INTEGER,
  final_url           TEXT,
  title               TEXT,
  cms                 TEXT,
  cms_version         TEXT,
  server              TEXT,
  hosting             TEXT,
  backend_language    TEXT,
  ip_address          TEXT,
  email_provider      TEXT,
  mobile_score        INTEGER,                       -- PageSpeed Insights, 0..100
  tls_valid_until     TEXT,                          -- YYYY-MM-DD
  domain_expires_on   TEXT,                          -- YYYY-MM-DD
  registrar           TEXT,
  favicon_url         TEXT,
  archived_at         TEXT,
  created_at          TEXT NOT NULL,
  updated_at          TEXT NOT NULL
);
CREATE INDEX idx_sites_regdomain ON sites(registrable_domain);
CREATE INDEX idx_sites_fetched ON sites(last_fetched_at);

CREATE TABLE jobs (
  id                INTEGER PRIMARY KEY,
  type              TEXT NOT NULL CHECK (type IN ('profile','crawl')),
  site_id           INTEGER REFERENCES sites(id) ON DELETE CASCADE,
  status            TEXT NOT NULL DEFAULT 'queued'
                    CHECK (status IN ('queued','running','succeeded','partial','failed','canceled')),
  params_json       TEXT NOT NULL DEFAULT '{}',
  progress_current  INTEGER,
  progress_total    INTEGER,
  progress_label    TEXT,
  result_json       TEXT,
  error             TEXT,
  exit_code         INTEGER,
  attempts          INTEGER NOT NULL DEFAULT 0,
  log_path          TEXT,                            -- relative to the data folder
  heartbeat_at      TEXT,
  created_at        TEXT NOT NULL,
  started_at        TEXT,
  finished_at       TEXT
);
CREATE INDEX idx_jobs_status ON jobs(status, created_at);
CREATE INDEX idx_jobs_site ON jobs(site_id, created_at);

CREATE TABLE profile_snapshots (
  id              INTEGER PRIMARY KEY,
  site_id         INTEGER NOT NULL REFERENCES sites(id) ON DELETE CASCADE,
  job_id          INTEGER REFERENCES jobs(id) ON DELETE SET NULL,
  fetched_at      TEXT NOT NULL,
  outcome         TEXT NOT NULL CHECK (outcome IN ('ok','partial','failed')),
  schema_version  INTEGER NOT NULL,
  tool_version    TEXT,
  requested_url   TEXT NOT NULL,
  final_url       TEXT,
  http_status     INTEGER,
  duration_ms     INTEGER,
  error_summary   TEXT,
  summary_json    TEXT NOT NULL,                     -- normalized by site_profile/summary.py
  raw_json        TEXT NOT NULL,                     -- the profiler report as written
  created_at      TEXT NOT NULL
);
CREATE INDEX idx_snapshots_site ON profile_snapshots(site_id, fetched_at);

CREATE TABLE snapshot_technologies (
  snapshot_id  INTEGER NOT NULL REFERENCES profile_snapshots(id) ON DELETE CASCADE,
  category     TEXT NOT NULL,
  name         TEXT NOT NULL,
  version      TEXT,
  confidence   TEXT NOT NULL,
  PRIMARY KEY (snapshot_id, category, name)
);
CREATE INDEX idx_snaptech_name ON snapshot_technologies(name);

CREATE TABLE speed_checks (
  id            INTEGER PRIMARY KEY,
  site_id       INTEGER NOT NULL REFERENCES sites(id) ON DELETE CASCADE,
  strategy      TEXT NOT NULL DEFAULT 'mobile',
  score         INTEGER,
  metrics_json  TEXT,
  error         TEXT,
  fetched_at    TEXT NOT NULL
);
CREATE INDEX idx_speed_site ON speed_checks(site_id, fetched_at);

CREATE TABLE activity (
  id          INTEGER PRIMARY KEY,
  site_id     INTEGER REFERENCES sites(id) ON DELETE CASCADE,
  type        TEXT NOT NULL,
  message     TEXT NOT NULL,
  data_json   TEXT,
  created_at  TEXT NOT NULL
);
CREATE INDEX idx_activity_site ON activity(site_id, id);

CREATE TABLE discovery_runs (
  id            INTEGER PRIMARY KEY,
  source        TEXT NOT NULL,                       -- 'langsearch'
  query         TEXT NOT NULL,
  params_json   TEXT NOT NULL DEFAULT '{}',
  result_count  INTEGER NOT NULL DEFAULT 0,
  new_count     INTEGER NOT NULL DEFAULT 0,
  api_units     INTEGER NOT NULL DEFAULT 0,
  error         TEXT,
  created_at    TEXT NOT NULL
);

CREATE TABLE discovery_results (
  id        INTEGER PRIMARY KEY,
  run_id    INTEGER NOT NULL REFERENCES discovery_runs(id) ON DELETE CASCADE,
  site_id   INTEGER REFERENCES sites(id) ON DELETE SET NULL,
  position  INTEGER NOT NULL,
  url       TEXT NOT NULL,
  title     TEXT,
  snippet   TEXT,
  raw_json  TEXT
);
CREATE INDEX idx_discovery_results_run ON discovery_results(run_id, position);

CREATE TABLE api_usage (
  service  TEXT NOT NULL,                            -- 'langsearch', 'gmail'
  period   TEXT NOT NULL,                            -- 'YYYY-MM'
  units    INTEGER NOT NULL DEFAULT 0,
  PRIMARY KEY (service, period)
);

CREATE TABLE mail_accounts (
  id            INTEGER PRIMARY KEY,
  email         TEXT NOT NULL UNIQUE,
  history_id    TEXT,
  last_sync_at  TEXT,
  status        TEXT NOT NULL DEFAULT 'connected',
  created_at    TEXT NOT NULL
);

CREATE TABLE mail_messages (
  id               INTEGER PRIMARY KEY,
  account_id       INTEGER NOT NULL REFERENCES mail_accounts(id) ON DELETE CASCADE,
  gmail_id         TEXT NOT NULL,
  thread_id        TEXT NOT NULL,
  site_id          INTEGER REFERENCES sites(id) ON DELETE SET NULL,
  direction        TEXT NOT NULL CHECK (direction IN ('sent','received')),
  from_addr        TEXT,
  to_addrs         TEXT,
  subject          TEXT,
  snippet          TEXT,
  sent_at          TEXT,
  has_attachments  INTEGER NOT NULL DEFAULT 0,
  is_bounce        INTEGER NOT NULL DEFAULT 0,
  is_auto_reply    INTEGER NOT NULL DEFAULT 0,
  linked_manually  INTEGER NOT NULL DEFAULT 0,
  UNIQUE (account_id, gmail_id)
);
CREATE INDEX idx_mail_site ON mail_messages(site_id, sent_at);

CREATE TABLE app_settings (
  name   TEXT PRIMARY KEY,
  value  TEXT NOT NULL                               -- JSON
);
