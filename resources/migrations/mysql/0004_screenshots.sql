-- Screenshots kept on this computer; the database stores paths relative to the data folder.
-- Keep in step with sqlite/0004_screenshots.sql.

CREATE TABLE screenshots (
  id          BIGINT AUTO_INCREMENT PRIMARY KEY,
  site_id     BIGINT NOT NULL,
  kind        VARCHAR(16) NOT NULL,
  source      VARCHAR(20) NOT NULL,
  path        VARCHAR(500) NOT NULL,
  created_at  DATETIME(3) NOT NULL,
  KEY idx_screenshots_site (site_id, kind),
  CONSTRAINT fk_screenshots_site FOREIGN KEY (site_id) REFERENCES sites(id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
