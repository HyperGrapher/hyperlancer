// Small shared vocabulary used by badges and filters.
export const STATUSES = [
  "none",
  "design",
  "waiting",
  "done",
  "canceled",
] as const
export type Status = (typeof STATUSES)[number]

export type Outcome = "ok" | "partial" | "failed"
export type JobStatus =
  "queued" | "running" | "succeeded" | "partial" | "failed" | "canceled"
export type Priority = "low" | "normal" | "high"
