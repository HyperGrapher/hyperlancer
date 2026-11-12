-- Deadline per site, a title on notes, and the "new" status becomes "none".
-- Keep in step with sqlite/0003_deadline_notes.sql.

ALTER TABLE sites
  ADD COLUMN deadline_on VARCHAR(10) NULL,
  ALTER COLUMN status SET DEFAULT 'none';
ALTER TABLE notes ADD COLUMN title VARCHAR(200) NOT NULL DEFAULT '';
UPDATE sites SET status = 'none' WHERE status = 'new';
