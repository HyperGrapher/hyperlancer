-- The last browser render of a website: what the page really loaded.
-- Keep in step with sqlite/0007_renders.sql.

CREATE TABLE renders (
  id             BIGINT AUTO_INCREMENT PRIMARY KEY,
  site_id        BIGINT NOT NULL,
  job_id         BIGINT,
  title          VARCHAR(500) NOT NULL DEFAULT '',
  load_ms        INT NOT NULL DEFAULT 0,
  request_count  INT NOT NULL DEFAULT 0,
  trackers_json  MEDIUMTEXT NOT NULL,
  created_at     DATETIME(3) NOT NULL,
  KEY idx_renders_site (site_id, id),
  CONSTRAINT fk_renders_site FOREIGN KEY (site_id) REFERENCES sites(id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
