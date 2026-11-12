-- Deadline per site, a title on notes, and the "new" status becomes "none".

ALTER TABLE sites ADD COLUMN deadline_on TEXT;                      -- YYYY-MM-DD
ALTER TABLE notes ADD COLUMN title TEXT NOT NULL DEFAULT '';
UPDATE sites SET status = 'none' WHERE status = 'new';
