import { useEffect, useRef } from "react"
import { RotateCcwIcon, XIcon } from "lucide-react"
import { toast } from "sonner"

import { useCancelJobInList, useJobLog, useRetryJob } from "@/api/hooks/jobs"
import { useJob } from "@/api/hooks/system"
import type { Job } from "@/api/types"
import { JobStatusBadge } from "@/components/common/badges"
import { CopyButton } from "@/components/common/misc"
import { Button } from "@/components/ui/button"
import { Progress } from "@/components/ui/progress"
import {
  Sheet,
  SheetContent,
  SheetDescription,
  SheetHeader,
  SheetTitle,
} from "@/components/ui/sheet"
import { relativeTime } from "@/lib/format"

export const isRunning = (job: Pick<Job, "status">) =>
  job.status === "queued" || job.status === "running"

export function percent(job: Job): number | null {
  const { current, total } = job.progress
  return current !== null && total
    ? Math.min(100, (current / total) * 100)
    : null
}

function duration(job: Job): string | null {
  if (!job.startedAt) return null
  const end = job.finishedAt ? new Date(job.finishedAt) : new Date()
  const seconds = Math.max(
    0,
    Math.round((end.getTime() - new Date(job.startedAt).getTime()) / 1000)
  )
  return seconds < 60
    ? `${seconds}s`
    : `${Math.floor(seconds / 60)}m ${seconds % 60}s`
}

function JobActions({ job }: { job: Job }) {
  const cancel = useCancelJobInList()
  const retry = useRetryJob()
  if (isRunning(job)) {
    return (
      <Button
        variant="outline"
        size="sm"
        disabled={cancel.isPending || job.cancelRequested}
        onClick={() =>
          cancel.mutate(job.id, { onError: (e) => toast.error(e.message) })
        }
      >
        <XIcon /> {job.cancelRequested ? "Canceling…" : "Cancel"}
      </Button>
    )
  }
  return (
    <Button
      variant="outline"
      size="sm"
      disabled={retry.isPending}
      onClick={() =>
        retry.mutate(job.id, {
          onSuccess: ({ job: next }) =>
            toast.success(`Queued again as job #${next.id}`),
          onError: (e) => toast.error(e.message),
        })
      }
    >
      <RotateCcwIcon /> Retry
    </Button>
  )
}

export { JobActions }

function Log({ job }: { job: Job }) {
  const { text, error } = useJobLog(job.id, isRunning(job))
  const box = useRef<HTMLPreElement>(null)
  const stick = useRef(true)

  useEffect(() => {
    const el = box.current
    if (el && stick.current) el.scrollTop = el.scrollHeight
  }, [text])

  return (
    <div className="flex min-h-0 flex-1 flex-col">
      <div className="mb-1 flex items-center justify-between text-xs text-muted-foreground">
        <span>Log</span>
        {text && <CopyButton value={text} />}
      </div>
      <pre
        ref={box}
        onScroll={(event) => {
          const el = event.currentTarget
          stick.current = el.scrollHeight - el.scrollTop - el.clientHeight < 24
        }}
        className="min-h-40 flex-1 overflow-auto rounded-lg border border-border bg-muted/40 p-3 font-mono text-xs break-words whitespace-pre-wrap"
        aria-live="off"
      >
        {text ||
          (error
            ? ""
            : isRunning(job)
              ? "Waiting for output…"
              : "No log output.")}
        {error && <span className="text-destructive">{error}</span>}
      </pre>
    </div>
  )
}

export function JobDrawer({
  jobId,
  onClose,
}: {
  jobId: number | null
  onClose: () => void
}) {
  const { data: job } = useJob(jobId)
  return (
    <Sheet open={jobId !== null} onOpenChange={(open) => !open && onClose()}>
      <SheetContent
        side="right"
        className="flex w-full flex-col gap-4 sm:max-w-xl"
      >
        {job ? (
          <>
            <SheetHeader className="p-0">
              <SheetTitle className="flex items-center gap-2">
                Job #{job.id} · {job.type}
                <JobStatusBadge status={job.status} />
              </SheetTitle>
              <SheetDescription>
                Created {relativeTime(job.createdAt)}
                {duration(job) && ` · ran ${duration(job)}`}
              </SheetDescription>
            </SheetHeader>
            <div className="flex items-center gap-3">
              <JobActions job={job} />
              {isRunning(job) && percent(job) !== null && (
                <>
                  <Progress value={percent(job) ?? 0} className="flex-1" />
                  <span className="font-mono text-xs text-muted-foreground">
                    {job.progress.current}/{job.progress.total}
                    {job.progress.label && ` · ${job.progress.label}`}
                  </span>
                </>
              )}
            </div>
            {job.error && (
              <p className="rounded-lg border border-rose-500/30 bg-rose-500/10 px-3 py-2 text-sm text-rose-700 dark:text-rose-300">
                {job.error}
              </p>
            )}
            {job.stats && (
              <dl className="grid grid-cols-2 gap-x-4 gap-y-1 text-sm sm:grid-cols-3">
                {Object.entries(job.stats).map(([key, value]) => (
                  <div key={key}>
                    <dt className="text-xs text-muted-foreground">{key}</dt>
                    <dd className="font-mono">{value}</dd>
                  </div>
                ))}
              </dl>
            )}
            <Log key={job.id} job={job} />
          </>
        ) : (
          <SheetHeader className="p-0">
            <SheetTitle>Job</SheetTitle>
            <SheetDescription>Loading…</SheetDescription>
          </SheetHeader>
        )}
      </SheetContent>
    </Sheet>
  )
}
