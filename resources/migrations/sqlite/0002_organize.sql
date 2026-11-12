-- Lead status, category, tags, notes and to-dos per site.

CREATE TABLE categories (
  id          INTEGER PRIMARY KEY,
  name        TEXT NOT NULL UNIQUE,
  created_at  TEXT NOT NULL
);

ALTER TABLE sites ADD COLUMN status TEXT NOT NULL DEFAULT 'new';
ALTER TABLE sites ADD COLUMN category_id INTEGER REFERENCES categories(id) ON DELETE SET NULL;
CREATE INDEX idx_sites_status ON sites(status);

CREATE TABLE tags (
  id          INTEGER PRIMARY KEY,
  name        TEXT NOT NULL UNIQUE,
  created_at  TEXT NOT NULL
);

CREATE TABLE site_tags (
  site_id  INTEGER NOT NULL REFERENCES sites(id) ON DELETE CASCADE,
  tag_id   INTEGER NOT NULL REFERENCES tags(id) ON DELETE CASCADE,
  PRIMARY KEY (site_id, tag_id)
);
CREATE INDEX idx_site_tags_tag ON site_tags(tag_id);

CREATE TABLE notes (
  id          INTEGER PRIMARY KEY,
  site_id     INTEGER NOT NULL REFERENCES sites(id) ON DELETE CASCADE,
  body        TEXT NOT NULL,
  created_at  TEXT NOT NULL,
  updated_at  TEXT NOT NULL
);
CREATE INDEX idx_notes_site ON notes(site_id, id);

CREATE TABLE todos (
  id          INTEGER PRIMARY KEY,
  site_id     INTEGER NOT NULL REFERENCES sites(id) ON DELETE CASCADE,
  title       TEXT NOT NULL,
  due_on      TEXT,                                  -- YYYY-MM-DD
  done_at     TEXT,
  created_at  TEXT NOT NULL
);
CREATE INDEX idx_todos_site ON todos(site_id, id);
