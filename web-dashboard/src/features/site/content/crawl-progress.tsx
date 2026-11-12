import { useEffect, useMemo, useRef, useState } from "react"
import { XIcon } from "lucide-react"
import { toast } from "sonner"

import { useCancelCrawl } from "@/api/hooks/crawls"
import { useJobLog } from "@/api/hooks/jobs"
import type { CrawlDetail } from "@/api/types"
import { Button } from "@/components/ui/button"
import { Progress } from "@/components/ui/progress"
import { formatBytes } from "@/lib/format"

const LEVELS = ["all", "warn", "error"] as const

function Counter({ label, value }: { label: string; value: string }) {
  return (
    <div className="rounded-lg border bg-card px-3 py-2">
      <div className="text-xs text-muted-foreground">{label}</div>
      <div className="text-lg font-semibold tabular-nums">{value}</div>
    </div>
  )
}

function useElapsed(startedAt: string | null, active: boolean) {
  const [now, setNow] = useState(() => Date.now())
  useEffect(() => {
    if (!active) return
    const timer = setInterval(() => setNow(Date.now()), 1000)
    return () => clearInterval(timer)
  }, [active])
  if (!startedAt) return null
  const seconds = Math.max(0, Math.round((now - Date.parse(startedAt)) / 1000))
  return `${Math.floor(seconds / 60)}:${String(seconds % 60).padStart(2, "0")}`
}

/** Live view of a queued or running crawl. Mount with key={crawl.id} so the log starts empty. */
export function CrawlProgress({ crawl }: { crawl: CrawlDetail }) {
  const job = crawl.job
  const cancel = useCancelCrawl(crawl.siteId)
  const { text } = useJobLog(crawl.jobId, true)
  const [level, setLevel] = useState<(typeof LEVELS)[number]>("all")
  const box = useRef<HTMLPreElement>(null)
  const elapsed = useElapsed(job?.startedAt ?? crawl.startedAt, true)

  const lines = useMemo(
    () =>
      text
        .split("\n")
        .filter(Boolean)
        .filter((line) => {
          if (level === "all") return true
          if (level === "error") return / ERROR /.test(line)
          return / (WARN|ERROR) /.test(line)
        })
        .slice(-200),
    [text, level]
  )
  useEffect(() => {
    const el = box.current
    if (el) el.scrollTop = el.scrollHeight
  }, [lines])

  const stats = job?.stats ?? {}
  const current = job?.progress.current ?? 0
  const total = job?.progress.total ?? null
  const value = total ? Math.min(100, (current / total) * 100) : null

  return (
    <div className="grid gap-4">
      <div className="flex items-center gap-3">
        <Progress
          value={value}
          aria-label="Crawl progress"
          className="flex-1"
        />
        <span className="text-sm text-muted-foreground tabular-nums">
          {total ? `${current} / ${total} pages` : "starting…"}
        </span>
        {job && (
          <Button
            variant="outline"
            size="sm"
            disabled={cancel.isPending || crawl.status === "canceling"}
            onClick={() =>
              cancel.mutate(job.id, {
                onError: (e) => toast.error(e.message),
              })
            }
          >
            <XIcon /> Cancel
          </Button>
        )}
      </div>
      <div className="grid grid-cols-2 gap-2 sm:grid-cols-5">
        <Counter label="Pages" value={String(stats.pages ?? 0)} />
        <Counter label="Images" value={String(stats.images ?? 0)} />
        <Counter label="Files" value={String(stats.files ?? 0)} />
        <Counter label="Downloaded" value={formatBytes(stats.bytes ?? 0)} />
        <Counter label="Errors" value={String(stats.errors ?? 0)} />
      </div>
      <div className="flex flex-wrap items-center justify-between gap-2 text-xs text-muted-foreground">
        <span
          role="status"
          aria-live="polite"
          className="min-w-0 truncate font-mono"
        >
          {job?.progress.label ?? "waiting for the worker…"}
        </span>
        <span>Elapsed {elapsed}</span>
      </div>
      <div className="grid gap-1.5">
        <div className="flex items-center gap-1">
          <span className="mr-auto text-sm font-medium">Log</span>
          {LEVELS.map((l) => (
            <Button
              key={l}
              size="xs"
              variant={level === l ? "secondary" : "ghost"}
              onClick={() => setLevel(l)}
            >
              {l}
            </Button>
          ))}
        </div>
        <pre
          ref={box}
          tabIndex={0}
          aria-label="Crawl log"
          className="h-64 overflow-auto rounded-lg border bg-muted/30 p-3 font-mono text-xs leading-relaxed"
        >
          {lines.join("\n") || "No log lines yet."}
        </pre>
      </div>
    </div>
  )
}
