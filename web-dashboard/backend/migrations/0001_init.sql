CREATE TABLE IF NOT EXISTS schema_migrations (
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
