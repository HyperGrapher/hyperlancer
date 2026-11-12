import { useMutation, useQuery, useQueryClient } from "@tanstack/react-query"

import { api } from "@/api/client"
import type { BackupInfo, Health, Job } from "@/api/types"

export function useHealth() {
  return useQuery({
    queryKey: ["health"],
    queryFn: ({ signal }) => api<Health>("/health", { signal }),
    retry: false,
  })
}

export function useBackups() {
  return useQuery({
    queryKey: ["backups"],
    queryFn: ({ signal }) =>
      api<{ items: BackupInfo[] }>("/backups", { signal }).then((r) => r.items),
  })
}

export function useCreateBackup() {
  const queryClient = useQueryClient()
  return useMutation({
    mutationFn: () => api<BackupInfo>("/backups", { method: "POST" }),
    onSuccess: () => queryClient.invalidateQueries({ queryKey: ["backups"] }),
  })
}

const ACTIVE: Job["status"][] = ["queued", "running"]

export function useJob(id: number | null) {
  return useQuery({
    queryKey: ["job", id],
    enabled: id !== null,
    queryFn: ({ signal }) => api<Job>(`/jobs/${id}`, { signal }),
    refetchInterval: (query) =>
      query.state.data && !ACTIVE.includes(query.state.data.status)
        ? false
        : 1000,
  })
}

export function useStartSpawnTest() {
  const queryClient = useQueryClient()
  return useMutation({
    mutationFn: (seconds: number) =>
      api<{ job: Job }>("/system/spawn-test", {
        method: "POST",
        body: { seconds },
      }),
    onSuccess: () => queryClient.invalidateQueries({ queryKey: ["health"] }),
  })
}

export function useCancelJob() {
  const queryClient = useQueryClient()
  return useMutation({
    mutationFn: (id: number) =>
      api<Job>(`/jobs/${id}/cancel`, { method: "POST" }),
    onSuccess: (job) => queryClient.setQueryData(["job", job.id], job),
  })
}
