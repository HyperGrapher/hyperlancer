-- Named combinations of search, filters and sort for the Websites list.

CREATE TABLE saved_views (
  id           INTEGER PRIMARY KEY,
  name         TEXT NOT NULL UNIQUE,
  config_json  TEXT NOT NULL,
  created_at   TEXT NOT NULL
);
