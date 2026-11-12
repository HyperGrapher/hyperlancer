import { useEffect, useRef } from "react"
import { useNavigate } from "@tanstack/react-router"
import { toast } from "sonner"

import { api } from "@/api/client"
import { useActiveJobs } from "@/api/hooks/jobs"
import type { Job } from "@/api/types"

const LABELS: Record<Job["type"], string> = {
  profile: "Profile fetch",
  crawl: "Content download",
  archive: "ZIP archive",
  backup: "Backup",
  selfcheck: "Worker spawn test",
}

/** Shows a toast when a job that was active disappears from the active list. Renders nothing. */
export function JobWatcher() {
  const { data } = useActiveJobs()
  const navigate = useNavigate()
  const seen = useRef<Set<number> | null>(null)

  useEffect(() => {
    if (!data) return
    const current = new Set(data.items.map((job) => job.id))
    const previous = seen.current
    seen.current = current
    if (previous === null) return // first load: do not announce jobs that finished before we looked
    for (const id of previous) {
      if (current.has(id)) continue
      api<Job>(`/jobs/${id}`)
        .then((job) => announce(job))
        .catch(() => undefined)
    }

    function announce(job: Job) {
      const label = `${LABELS[job.type]} #${job.id}`
      const action = { label: "Open", onClick: () => navigate({ to: "/jobs" }) }
      if (job.status === "succeeded")
        toast.success(`${label} finished`, { action })
      else if (job.status === "partial")
        toast.warning(`${label} finished with problems`, { action })
      else if (job.status === "canceled") toast(`${label} canceled`)
      else
        toast.error(`${label} failed${job.error ? `: ${job.error}` : ""}`, {
          action,
        })
    }
  }, [data, navigate])

  return null
}
