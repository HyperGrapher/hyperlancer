import { cn } from "cn"

import type { JobStatus, Outcome, Priority, Status } from "@/lib/domain"
import type { CrawlStatus } from "@/api/types"

const pill =
  "inline-flex h-5 items-center gap-1.5 rounded-full border px-2 text-xs font-medium whitespace-nowrap"

const statusStyles: Record<Status, string> = {
  none: "border-zinc-500/30 bg-zinc-500/10 text-zinc-700 dark:text-zinc-300",
  design: "border-sky-500/30 bg-sky-500/10 text-sky-700 dark:text-sky-300",
  waiting:
    "border-amber-500/30 bg-amber-500/10 text-amber-700 dark:text-amber-300",
  done: "border-emerald-500/30 bg-emerald-500/10 text-emerald-700 dark:text-emerald-300",
  canceled:
    "border-rose-500/30 bg-rose-500/10 text-rose-700 dark:text-rose-300",
}

const statusDots: Record<Status, string> = {
  none: "bg-zinc-400",
  design: "bg-sky-400",
  waiting: "bg-amber-400",
  done: "bg-emerald-400",
  canceled: "bg-rose-400",
}

export function StatusBadge({
  status,
  className,
}: {
  status: Status
  className?: string
}) {
  return (
    <span className={cn(pill, statusStyles[status], className)}>
      <span className={cn("size-1.5 rounded-full", statusDots[status])} />
      {status}
    </span>
  )
}

export function StatusDot({ status }: { status: Status }) {
  return <span className={cn("size-2 rounded-full", statusDots[status])} />
}

const outcomeStyles: Record<Outcome, string> = {
  ok: "border-emerald-500/30 bg-emerald-500/10 text-emerald-700 dark:text-emerald-300",
  partial:
    "border-amber-500/30 bg-amber-500/10 text-amber-700 dark:text-amber-300",
  failed: "border-rose-500/30 bg-rose-500/10 text-rose-700 dark:text-rose-300",
}

export function OutcomeBadge({ outcome }: { outcome: Outcome | null }) {
  if (!outcome) {
    return (
      <span className={cn(pill, "border-border text-muted-foreground")}>
        never
      </span>
    )
  }
  return <span className={cn(pill, outcomeStyles[outcome])}>{outcome}</span>
}

const jobStyles: Record<JobStatus, string> = {
  queued: "border-zinc-500/30 bg-zinc-500/10 text-zinc-700 dark:text-zinc-300",
  running: "border-sky-500/30 bg-sky-500/10 text-sky-700 dark:text-sky-300",
  succeeded:
    "border-emerald-500/30 bg-emerald-500/10 text-emerald-700 dark:text-emerald-300",
  partial:
    "border-amber-500/30 bg-amber-500/10 text-amber-700 dark:text-amber-300",
  failed: "border-rose-500/30 bg-rose-500/10 text-rose-700 dark:text-rose-300",
  canceled:
    "border-zinc-500/30 bg-zinc-500/10 text-zinc-500 dark:text-zinc-400",
}

const crawlStyles: Record<CrawlStatus, string> = {
  ...jobStyles,
  canceling:
    "border-amber-500/30 bg-amber-500/10 text-amber-700 dark:text-amber-300",
  done: jobStyles.succeeded,
}

export function CrawlStatusBadge({ status }: { status: CrawlStatus }) {
  const live = status === "running" || status === "canceling"
  return (
    <span className={cn(pill, crawlStyles[status])}>
      {live && (
        <span className="size-1.5 animate-pulse rounded-full bg-sky-400" />
      )}
      {status}
    </span>
  )
}

export function JobStatusBadge({ status }: { status: JobStatus }) {
  return (
    <span className={cn(pill, jobStyles[status])}>
      {status === "running" && (
        <span className="size-1.5 animate-pulse rounded-full bg-sky-400" />
      )}
      {status}
    </span>
  )
}

const priorityStyles: Record<Priority, string> = {
  low: "text-muted-foreground",
  normal: "text-foreground/70",
  high: "text-rose-300",
}

export function PriorityLabel({ priority }: { priority: Priority }) {
  if (priority === "normal") return null
  return (
    <span
      className={cn("text-xs font-medium uppercase", priorityStyles[priority])}
    >
      {priority === "high" ? "! high" : "low"}
    </span>
  )
}

export function TagChip({ name }: { name: string }) {
  return (
    <span className="inline-flex h-5 items-center rounded-md border border-border bg-muted/40 px-1.5 text-xs text-muted-foreground">
      {name}
    </span>
  )
}

export function ConfidenceDot({
  confidence,
}: {
  confidence: "low" | "medium" | "high"
}) {
  const color =
    confidence === "high"
      ? "bg-emerald-400"
      : confidence === "medium"
        ? "bg-amber-400"
        : "bg-zinc-500"
  return (
    <span
      className={cn("size-1.5 rounded-full", color)}
      title={`${confidence} confidence`}
    />
  )
}
