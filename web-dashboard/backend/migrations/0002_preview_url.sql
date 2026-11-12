-- Where the redesign is previewed (for example a Vercel deployment), entered by hand once it is pushed.
ALTER TABLE sites ADD COLUMN preview_url TEXT CHECK (preview_url IS NULL OR length(preview_url) <= 500);
