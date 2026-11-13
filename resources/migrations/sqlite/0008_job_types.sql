-- Browser renders need the job type 'render'. SQLite cannot change a CHECK constraint, so the
-- table is rebuilt (the migration runner switches foreign keys off around it).

CREATE TABLE jobs_new (
  id                INTEGER PRIMARY KEY,
  type              TEXT NOT NULL CHECK (type IN ('profile','crawl','render')),
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
  log_path          TEXT,
  heartbeat_at      TEXT,
  created_at        TEXT NOT NULL,
  started_at        TEXT,
  finished_at       TEXT
);
INSERT INTO jobs_new SELECT * FROM jobs;
DROP TABLE jobs;
ALTER TABLE jobs_new RENAME TO jobs;
CREATE INDEX idx_jobs_status ON jobs(status, created_at);
CREATE INDEX idx_jobs_site ON jobs(site_id, created_at);
