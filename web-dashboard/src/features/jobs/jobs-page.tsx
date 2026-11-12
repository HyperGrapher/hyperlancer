import { useState } from "react"
import { Link } from "@tanstack/react-router"
import { WorkflowIcon } from "lucide-react"

import { isActive, useJobs, type JobFilter } from "@/api/hooks/jobs"
import { JobStatusBadge } from "@/components/common/badges"
import { EmptyState, PageHeader } from "@/components/common/misc"
import { Button } from "@/components/ui/button"
import { Progress } from "@/components/ui/progress"
import { relativeTime } from "@/lib/format"

import { JobActions, JobDrawer, percent } from "./job-drawer"

const FILTERS: { value: JobFilter; label: string }[] = [
  { value: "all", label: "All" },
  { value: "active", label: "Active" },
  { value: "finished", label: "Finished" },
]

export function JobsPage() {
  const [filter, setFilter] = useState<JobFilter>("all")
  const [openId, setOpenId] = useState<number | null>(null)
  const { data, error, isLoading } = useJobs(filter)

  return (
    <div>
      <PageHeader
        title="Jobs"
        description="Profile fetches, content downloads and ZIP archives run here in the background."
        actions={
          <div className="flex rounded-lg border border-border p-0.5">
            {FILTERS.map((item) => (
              <Button
                key={item.value}
                size="sm"
                variant={filter === item.value ? "secondary" : "ghost"}
                onClick={() => setFilter(item.value)}
              >
                {item.label}
              </Button>
            ))}
          </div>
        }
      />
      {isLoading && <p className="text-sm text-muted-foreground">Loading…</p>}
      {error && <p className="text-sm text-destructive">{error.message}</p>}
      {data && data.items.length === 0 && (
        <EmptyState
          icon={<WorkflowIcon />}
          title="No jobs"
          description={
            filter === "active"
              ? "Nothing is running right now."
              : "Jobs appear here when you fetch a profile or start a download. Settings → Health has a spawn test."
          }
        />
      )}
      {data && data.items.length > 0 && (
        <div className="divide-y divide-border rounded-xl border border-border bg-card/60">
          {data.items.map((job) => {
            const pct = percent(job)
            return (
              <div key={job.id} className="flex flex-col gap-2 px-4 py-3">
                <div className="flex flex-wrap items-center gap-3">
                  <button
                    type="button"
                    onClick={() => setOpenId(job.id)}
                    className="flex items-center gap-3 rounded text-left hover:underline"
                  >
                    <span className="font-mono text-xs text-muted-foreground">
                      #{job.id}
                    </span>
                    <span className="font-medium">{job.type}</span>
                  </button>
                  {job.siteId !== null && (
                    <Link
                      to="/sites/$siteId"
                      params={{ siteId: job.siteId }}
                      className="text-sm text-muted-foreground hover:text-foreground hover:underline"
                    >
                      {job.siteKey ?? `site ${job.siteId}`}
                    </Link>
                  )}
                  <JobStatusBadge status={job.status} />
                  <span className="ml-auto text-xs text-muted-foreground">
                    {relativeTime(job.createdAt)}
                  </span>
                  {(isActive(job) ||
                    job.status === "failed" ||
                    job.status === "partial") && <JobActions job={job} />}
                </div>
                {job.status === "running" && pct !== null && (
                  <div className="flex items-center gap-3">
                    <Progress value={pct} className="flex-1" />
                    <span className="shrink-0 font-mono text-xs text-muted-foreground">
                      {job.progress.label}
                    </span>
                  </div>
                )}
                {job.error && (
                  <p className="text-sm text-muted-foreground">{job.error}</p>
                )}
              </div>
            )
          })}
        </div>
      )}
      <JobDrawer jobId={openId} onClose={() => setOpenId(null)} />
    </div>
  )
}
