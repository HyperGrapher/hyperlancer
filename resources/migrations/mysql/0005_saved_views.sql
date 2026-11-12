-- Named combinations of search, filters and sort for the Websites list.
-- Keep in step with sqlite/0005_saved_views.sql.

CREATE TABLE saved_views (
  id           BIGINT AUTO_INCREMENT PRIMARY KEY,
  name         VARCHAR(120) NOT NULL,
  config_json  TEXT NOT NULL,
  created_at   DATETIME(3) NOT NULL,
  UNIQUE KEY uq_saved_views_name (name)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
