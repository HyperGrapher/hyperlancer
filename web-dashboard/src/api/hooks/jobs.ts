import { useEffect, useRef, useState } from "react"
import { useMutation, useQuery, useQueryClient } from "@tanstack/react-query"

import { api } from "@/api/client"
import type { Job } from "@/api/types"

export type JobFilter = "all" | "active" | "finished"

export const isActive = (job: Job) =>
  job.status === "queued" || job.status === "running"

interface JobList {
  items: Job[]
  total: number
}

/** Polls faster while something is running; the poll also lets the API restart a worker for queued jobs. */
export function useJobs(filter: JobFilter = "all", type?: string) {
  return useQuery({
    queryKey: ["jobs", filter, type ?? "all"],
    queryFn: ({ signal }) =>
      api<JobList>("/jobs", {
        query: { status: filter, type, perPage: 100 },
        signal,
      }),
    refetchInterval: (query) =>
      query.state.data?.items.some(isActive) ? 2000 : 8000,
  })
}

export function useActiveJobs() {
  return useQuery({
    queryKey: ["jobs", "active", "all"],
    queryFn: ({ signal }) =>
      api<JobList>("/jobs", {
        query: { status: "active", perPage: 100 },
        signal,
      }),
    refetchInterval: (query) => (query.state.data?.items.length ? 2000 : 10000),
  })
}

export function useRetryJob() {
  const queryClient = useQueryClient()
  return useMutation({
    mutationFn: (id: number) =>
      api<{ job: Job }>(`/jobs/${id}/retry`, { method: "POST" }),
    onSuccess: () => queryClient.invalidateQueries({ queryKey: ["jobs"] }),
  })
}

export function useCancelJobInList() {
  const queryClient = useQueryClient()
  return useMutation({
    mutationFn: (id: number) =>
      api<Job>(`/jobs/${id}/cancel`, { method: "POST" }),
    onSuccess: (job) => {
      queryClient.setQueryData(["job", job.id], job)
      return queryClient.invalidateQueries({ queryKey: ["jobs"] })
    },
  })
}

/** Follows job.log by byte offset until the job is finished and the end of the file was read.
 * Mount it with `key={jobId}`: state starts empty for each job instead of being reset in an effect. */
export function useJobLog(jobId: number | null, active: boolean) {
  const [text, setText] = useState("")
  const [error, setError] = useState<string | null>(null)
  const offset = useRef(0)
  const eof = useRef(false)

  useEffect(() => {
    if (jobId === null) return
    let stopped = false
    let timer: ReturnType<typeof setTimeout> | undefined

    async function poll() {
      try {
        const chunk = await api<{
          text: string
          nextOffset: number
          eof: boolean
        }>(`/jobs/${jobId}/log`, { query: { offset: offset.current } })
        if (stopped) return
        if (chunk.text) setText((previous) => previous + chunk.text)
        offset.current = chunk.nextOffset
        eof.current = chunk.eof
        setError(null)
      } catch (e) {
        if (!stopped)
          setError(e instanceof Error ? e.message : "Could not load the log.")
      }
      if (!stopped && !eof.current) timer = setTimeout(poll, 1000)
    }
    poll()
    return () => {
      stopped = true
      clearTimeout(timer)
    }
    // `active` re-runs the effect once when the job finishes, to read the tail written after the last poll.
  }, [jobId, active])

  return { text, error }
}
