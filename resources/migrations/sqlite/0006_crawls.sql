-- Content crawls: one row per finished crawl job; the files stay in the app data folder.

CREATE TABLE crawls (
  id            INTEGER PRIMARY KEY,
  site_id       INTEGER NOT NULL REFERENCES sites(id) ON DELETE CASCADE,
  job_id        INTEGER,
  status        TEXT NOT NULL,                       -- done, partial, failed
  stop_reason   TEXT,
  pages         INTEGER NOT NULL DEFAULT 0,
  images        INTEGER NOT NULL DEFAULT 0,
  files         INTEGER NOT NULL DEFAULT 0,
  bytes         INTEGER NOT NULL DEFAULT 0,
  errors        INTEGER NOT NULL DEFAULT 0,
  path          TEXT NOT NULL,                       -- folder relative to the data folder
  created_at    TEXT NOT NULL
);
CREATE INDEX idx_crawls_site ON crawls(site_id, id);
