import { z } from "zod"

import { STATUSES, type Status } from "@/lib/domain"

// Filters live in the URL (docs/PRD.md §5.5) and are sent to GET /api/sites as they are.
// Multi-value filters are comma-separated strings so links stay readable: ?status=design,waiting
// TanStack Router parses URL values as JSON, so "?q=2024" arrives as the number 2024 and "?archived=1" as 1.
// Everything is turned back into a string first so such values are not silently dropped.
const text = z
  .preprocess(
    (value) => (value == null ? undefined : String(value)),
    z.string().optional()
  )
  .catch(undefined)

export const sitesSearchSchema = z.object({
  q: text,
  status: text,
  category: text,
  tag: text,
  cms: text,
  outcome: text,
  sort: text,
  view: z.enum(["table", "cards"]).optional().catch(undefined),
  archived: z
    .preprocess(
      (value) => (value == null ? undefined : String(value)),
      z.literal("1").optional()
    )
    .catch(undefined),
  page: z.coerce.number().int().min(1).optional().catch(undefined),
})

export type SitesSearch = z.infer<typeof sitesSearchSchema>

export function splitList(value: string | undefined): string[] {
  return value ? value.split(",").filter(Boolean) : []
}

export function joinList(values: string[]): string | undefined {
  return values.length ? values.join(",") : undefined
}

export function parseStatuses(value: string | undefined): Status[] {
  return splitList(value).filter((item): item is Status =>
    (STATUSES as readonly string[]).includes(item)
  )
}

export const sortOptions = [
  { value: "-updated", label: "Recently updated" },
  { value: "-fetched", label: "Recently fetched" },
  { value: "domain", label: "Domain A–Z" },
  { value: "deadline", label: "Deadline" },
] as const
