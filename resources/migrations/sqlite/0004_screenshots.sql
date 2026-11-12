-- Screenshots kept on this computer; the database stores paths relative to the data folder.

CREATE TABLE screenshots (
  id          INTEGER PRIMARY KEY,
  site_id     INTEGER NOT NULL REFERENCES sites(id) ON DELETE CASCADE,
  kind        TEXT NOT NULL,                         -- 'mobile' or 'desktop'
  source      TEXT NOT NULL,                         -- 'pagespeed'
  path        TEXT NOT NULL,
  created_at  TEXT NOT NULL
);
CREATE INDEX idx_screenshots_site ON screenshots(site_id, kind);
