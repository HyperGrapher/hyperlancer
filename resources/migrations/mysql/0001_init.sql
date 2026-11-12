-- Hyperlancer schema v1 (MySQL 8 / MariaDB 10.5+). Keep in step with sqlite/0001_init.sql.
-- InnoDB, utf8mb4, times in UTC as DATETIME(3) written by the app.

CREATE TABLE IF NOT EXISTS schema_migrations (
  version     INT PRIMARY KEY,
  name        VARCHAR(200) NOT NULL,
  applied_at  DATETIME(3) NOT NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE sites (
  id                  BIGINT AUTO_INCREMENT PRIMARY KEY,
  site_key            VARCHAR(255) NOT NULL,
  registrable_domain  VARCHAR(255),
  url                 VARCHAR(2048) NOT NULL,
  name                VARCHAR(255),
  source              VARCHAR(20) NOT NULL DEFAULT 'manual',
  allow_private_net   TINYINT(1) NOT NULL DEFAULT 0,
  preview_url         VARCHAR(500),
  latest_snapshot_id  BIGINT,
  last_fetched_at     DATETIME(3),
  last_fetch_outcome  VARCHAR(10),
  http_status         INT,
  final_url           VARCHAR(2048),
  title               VARCHAR(1000),
  cms                 VARCHAR(120),
  cms_version         VARCHAR(60),
  server              VARCHAR(255),
  hosting             VARCHAR(255),
  backend_language    VARCHAR(120),
  ip_address          VARCHAR(64),
  email_provider      VARCHAR(120),
  mobile_score        INT,
  tls_valid_until     VARCHAR(10),
  domain_expires_on   VARCHAR(10),
  registrar           VARCHAR(255),
  favicon_url         VARCHAR(2048),
  archived_at         DATETIME(3),
  created_at          DATETIME(3) NOT NULL,
  updated_at          DATETIME(3) NOT NULL,
  UNIQUE KEY uq_sites_key (site_key),
  KEY idx_sites_regdomain (registrable_domain),
  KEY idx_sites_fetched (last_fetched_at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE jobs (
  id                BIGINT AUTO_INCREMENT PRIMARY KEY,
  type              VARCHAR(20) NOT NULL,
  site_id           BIGINT,
  status            VARCHAR(12) NOT NULL DEFAULT 'queued',
  params_json       JSON NOT NULL,
  progress_current  INT,
  progress_total    INT,
  progress_label    VARCHAR(255),
  result_json       JSON,
  error             TEXT,
  exit_code         INT,
  attempts          INT NOT NULL DEFAULT 0,
  log_path          VARCHAR(500),
  heartbeat_at      DATETIME(3),
  created_at        DATETIME(3) NOT NULL,
  started_at        DATETIME(3),
  finished_at       DATETIME(3),
  KEY idx_jobs_status (status, created_at),
  KEY idx_jobs_site (site_id, created_at),
  CONSTRAINT fk_jobs_site FOREIGN KEY (site_id) REFERENCES sites(id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE profile_snapshots (
  id              BIGINT AUTO_INCREMENT PRIMARY KEY,
  site_id         BIGINT NOT NULL,
  job_id          BIGINT,
  fetched_at      DATETIME(3) NOT NULL,
  outcome         VARCHAR(10) NOT NULL,
  schema_version  INT NOT NULL,
  tool_version    VARCHAR(40),
  requested_url   VARCHAR(2048) NOT NULL,
  final_url       VARCHAR(2048),
  http_status     INT,
  duration_ms     INT,
  error_summary   TEXT,
  summary_json    JSON NOT NULL,
  raw_json        LONGTEXT NOT NULL,
  created_at      DATETIME(3) NOT NULL,
  KEY idx_snapshots_site (site_id, fetched_at),
  CONSTRAINT fk_snapshots_site FOREIGN KEY (site_id) REFERENCES sites(id) ON DELETE CASCADE,
  CONSTRAINT fk_snapshots_job FOREIGN KEY (job_id) REFERENCES jobs(id) ON DELETE SET NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

ALTER TABLE sites ADD CONSTRAINT fk_sites_latest_snapshot
  FOREIGN KEY (latest_snapshot_id) REFERENCES profile_snapshots(id) ON DELETE SET NULL;

CREATE TABLE snapshot_technologies (
  snapshot_id  BIGINT NOT NULL,
  category     VARCHAR(100) NOT NULL,
  name         VARCHAR(150) NOT NULL,
  version      VARCHAR(60),
  confidence   VARCHAR(10) NOT NULL,
  PRIMARY KEY (snapshot_id, category, name),
  KEY idx_snaptech_name (name),
  CONSTRAINT fk_snaptech_snapshot FOREIGN KEY (snapshot_id) REFERENCES profile_snapshots(id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE speed_checks (
  id            BIGINT AUTO_INCREMENT PRIMARY KEY,
  site_id       BIGINT NOT NULL,
  strategy      VARCHAR(10) NOT NULL DEFAULT 'mobile',
  score         INT,
  metrics_json  JSON,
  error         TEXT,
  fetched_at    DATETIME(3) NOT NULL,
  KEY idx_speed_site (site_id, fetched_at),
  CONSTRAINT fk_speed_site FOREIGN KEY (site_id) REFERENCES sites(id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE activity (
  id          BIGINT AUTO_INCREMENT PRIMARY KEY,
  site_id     BIGINT,
  type        VARCHAR(60) NOT NULL,
  message     TEXT NOT NULL,
  data_json   JSON,
  created_at  DATETIME(3) NOT NULL,
  KEY idx_activity_site (site_id, id),
  CONSTRAINT fk_activity_site FOREIGN KEY (site_id) REFERENCES sites(id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE discovery_runs (
  id            BIGINT AUTO_INCREMENT PRIMARY KEY,
  source        VARCHAR(40) NOT NULL,
  query         VARCHAR(1000) NOT NULL,
  params_json   JSON NOT NULL,
  result_count  INT NOT NULL DEFAULT 0,
  new_count     INT NOT NULL DEFAULT 0,
  api_units     INT NOT NULL DEFAULT 0,
  error         TEXT,
  created_at    DATETIME(3) NOT NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE discovery_results (
  id        BIGINT AUTO_INCREMENT PRIMARY KEY,
  run_id    BIGINT NOT NULL,
  site_id   BIGINT,
  position  INT NOT NULL,
  url       VARCHAR(2048) NOT NULL,
  title     VARCHAR(1000),
  snippet   TEXT,
  raw_json  JSON,
  KEY idx_discovery_results_run (run_id, position),
  CONSTRAINT fk_dresults_run FOREIGN KEY (run_id) REFERENCES discovery_runs(id) ON DELETE CASCADE,
  CONSTRAINT fk_dresults_site FOREIGN KEY (site_id) REFERENCES sites(id) ON DELETE SET NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE api_usage (
  service  VARCHAR(40) NOT NULL,
  period   CHAR(7) NOT NULL,
  units    INT NOT NULL DEFAULT 0,
  PRIMARY KEY (service, period)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE mail_accounts (
  id            BIGINT AUTO_INCREMENT PRIMARY KEY,
  email         VARCHAR(255) NOT NULL,
  history_id    VARCHAR(40),
  last_sync_at  DATETIME(3),
  status        VARCHAR(20) NOT NULL DEFAULT 'connected',
  created_at    DATETIME(3) NOT NULL,
  UNIQUE KEY uq_mail_accounts_email (email)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE mail_messages (
  id               BIGINT AUTO_INCREMENT PRIMARY KEY,
  account_id       BIGINT NOT NULL,
  gmail_id         VARCHAR(40) NOT NULL,
  thread_id        VARCHAR(40) NOT NULL,
  site_id          BIGINT,
  direction        VARCHAR(10) NOT NULL,
  from_addr        VARCHAR(500),
  to_addrs         TEXT,
  subject          VARCHAR(1000),
  snippet          TEXT,
  sent_at          DATETIME(3),
  has_attachments  TINYINT(1) NOT NULL DEFAULT 0,
  is_bounce        TINYINT(1) NOT NULL DEFAULT 0,
  is_auto_reply    TINYINT(1) NOT NULL DEFAULT 0,
  linked_manually  TINYINT(1) NOT NULL DEFAULT 0,
  UNIQUE KEY uq_mail_gmail (account_id, gmail_id),
  KEY idx_mail_site (site_id, sent_at),
  CONSTRAINT fk_mail_account FOREIGN KEY (account_id) REFERENCES mail_accounts(id) ON DELETE CASCADE,
  CONSTRAINT fk_mail_site FOREIGN KEY (site_id) REFERENCES sites(id) ON DELETE SET NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE app_settings (
  name   VARCHAR(120) PRIMARY KEY,
  value  JSON NOT NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
