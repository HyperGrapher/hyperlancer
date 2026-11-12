-- Content crawls: one row per finished crawl job; the files stay in the app data folder.
-- Keep in step with sqlite/0006_crawls.sql.

CREATE TABLE crawls (
  id            BIGINT AUTO_INCREMENT PRIMARY KEY,
  site_id       BIGINT NOT NULL,
  job_id        BIGINT,
  status        VARCHAR(12) NOT NULL,
  stop_reason   VARCHAR(40),
  pages         INT NOT NULL DEFAULT 0,
  images        INT NOT NULL DEFAULT 0,
  files         INT NOT NULL DEFAULT 0,
  bytes         BIGINT NOT NULL DEFAULT 0,
  errors        INT NOT NULL DEFAULT 0,
  path          VARCHAR(500) NOT NULL,
  created_at    DATETIME(3) NOT NULL,
  KEY idx_crawls_site (site_id, id),
  CONSTRAINT fk_crawls_site FOREIGN KEY (site_id) REFERENCES sites(id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
