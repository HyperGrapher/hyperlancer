-- The last browser render of a website: what the page really loaded. Screenshots go to the
-- screenshots table; only the newest render is kept.

CREATE TABLE renders (
  id             INTEGER PRIMARY KEY,
  site_id        INTEGER NOT NULL REFERENCES sites(id) ON DELETE CASCADE,
  job_id         INTEGER,
  title          TEXT NOT NULL DEFAULT '',
  load_ms        INTEGER NOT NULL DEFAULT 0,
  request_count  INTEGER NOT NULL DEFAULT 0,
  trackers_json  TEXT NOT NULL,
  created_at     TEXT NOT NULL
);
CREATE INDEX idx_renders_site ON renders(site_id, id);
