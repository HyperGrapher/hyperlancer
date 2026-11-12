-- Lead status, category, tags, notes and to-dos per site. Keep in step with sqlite/0002_organize.sql.

CREATE TABLE categories (
  id          BIGINT AUTO_INCREMENT PRIMARY KEY,
  name        VARCHAR(120) NOT NULL,
  created_at  DATETIME(3) NOT NULL,
  UNIQUE KEY uq_categories_name (name)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

ALTER TABLE sites
  ADD COLUMN status VARCHAR(16) NOT NULL DEFAULT 'new',
  ADD COLUMN category_id BIGINT NULL,
  ADD KEY idx_sites_status (status),
  ADD CONSTRAINT fk_sites_category FOREIGN KEY (category_id) REFERENCES categories(id) ON DELETE SET NULL;

CREATE TABLE tags (
  id          BIGINT AUTO_INCREMENT PRIMARY KEY,
  name        VARCHAR(120) NOT NULL,
  created_at  DATETIME(3) NOT NULL,
  UNIQUE KEY uq_tags_name (name)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE site_tags (
  site_id  BIGINT NOT NULL,
  tag_id   BIGINT NOT NULL,
  PRIMARY KEY (site_id, tag_id),
  KEY idx_site_tags_tag (tag_id),
  CONSTRAINT fk_site_tags_site FOREIGN KEY (site_id) REFERENCES sites(id) ON DELETE CASCADE,
  CONSTRAINT fk_site_tags_tag FOREIGN KEY (tag_id) REFERENCES tags(id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE notes (
  id          BIGINT AUTO_INCREMENT PRIMARY KEY,
  site_id     BIGINT NOT NULL,
  body        TEXT NOT NULL,
  created_at  DATETIME(3) NOT NULL,
  updated_at  DATETIME(3) NOT NULL,
  KEY idx_notes_site (site_id, id),
  CONSTRAINT fk_notes_site FOREIGN KEY (site_id) REFERENCES sites(id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE todos (
  id          BIGINT AUTO_INCREMENT PRIMARY KEY,
  site_id     BIGINT NOT NULL,
  title       VARCHAR(500) NOT NULL,
  due_on      VARCHAR(10),
  done_at     DATETIME(3),
  created_at  DATETIME(3) NOT NULL,
  KEY idx_todos_site (site_id, id),
  CONSTRAINT fk_todos_site FOREIGN KEY (site_id) REFERENCES sites(id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
