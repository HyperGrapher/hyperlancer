import {
  keepPreviousData,
  useMutation,
  useQuery,
  useQueryClient,
} from "@tanstack/react-query"

import { api } from "@/api/client"
import type {
  Category,
  Job,
  SavedView,
  Snapshot,
  SnapshotDiffResult,
  SnapshotMeta,
  SiteDetail,
  SiteList,
  SiteStatus,
  Tag,
} from "@/api/types"

const ACTIVE = ["queued", "running"]

export function useSites(search: Record<string, string | number | undefined>) {
  return useQuery({
    queryKey: ["sites", "list", search],
    queryFn: ({ signal }) => api<SiteList>("/sites", { query: search, signal }),
    placeholderData: keepPreviousData,
  })
}

/** Polls while a profile job is queued or running, so the page updates by itself when it finishes. */
export function useSite(id: number) {
  return useQuery({
    queryKey: ["sites", "detail", id],
    queryFn: ({ signal }) => api<SiteDetail>(`/sites/${id}`, { signal }),
    refetchInterval: (query) => {
      const job = query.state.data?.activeJob
      return job && ACTIVE.includes(job.status) ? 1500 : false
    },
  })
}

export function useCheckUrl(url: string) {
  return useQuery({
    queryKey: ["sites", "check", url],
    enabled: url.trim().length > 2,
    retry: false,
    queryFn: ({ signal }) =>
      api<{
        normalizedUrl: string
        siteKey: string
        registrableDomain: string | null
        blockedReason: string | null
        existing: { id: number; siteKey: string } | null
        related: { id: number; siteKey: string }[]
      }>("/sites/check-url", { query: { url }, signal }),
  })
}

function useInvalidate() {
  const queryClient = useQueryClient()
  return () => queryClient.invalidateQueries({ queryKey: ["sites"] })
}

export interface NewSite {
  url: string
  name?: string
  status?: SiteStatus
  categoryId?: number | null
  tags?: string[]
  fetch?: boolean
  allowPrivateNet?: boolean
  allowDuplicate?: boolean
}

export function useCreateSite() {
  const invalidate = useInvalidate()
  const queryClient = useQueryClient()
  return useMutation({
    mutationFn: (body: NewSite) =>
      api<{ site: SiteDetail; job: Job | null }>("/sites", {
        method: "POST",
        body,
      }),
    onSuccess: () => {
      invalidate()
      queryClient.invalidateQueries({ queryKey: ["tags"] })
    },
  })
}

export interface SitePatch {
  name?: string | null
  url?: string
  status?: SiteStatus
  categoryId?: number | null
  tags?: string[]
  deadline?: string | null
  previewUrl?: string | null
  allowPrivateNet?: boolean
}

export function usePatchSite(id: number) {
  const invalidate = useInvalidate()
  const queryClient = useQueryClient()
  return useMutation({
    mutationFn: (body: SitePatch) =>
      api<SiteDetail>(`/sites/${id}`, { method: "PATCH", body }),
    onSuccess: (site) => {
      queryClient.setQueryData(["sites", "detail", id], site)
      invalidate()
      queryClient.invalidateQueries({ queryKey: ["tags"] })
    },
  })
}

export function useArchiveSite() {
  const invalidate = useInvalidate()
  return useMutation({
    mutationFn: (id: number) => api<void>(`/sites/${id}`, { method: "DELETE" }),
    onSuccess: invalidate,
  })
}

export function useRestoreSite() {
  const invalidate = useInvalidate()
  return useMutation({
    mutationFn: (id: number) =>
      api<SiteDetail>(`/sites/${id}/restore`, { method: "POST" }),
    onSuccess: invalidate,
  })
}

export function useDeleteSite() {
  const invalidate = useInvalidate()
  return useMutation({
    mutationFn: ({ id, confirm }: { id: number; confirm: string }) =>
      api<void>(`/sites/${id}`, {
        method: "DELETE",
        query: { mode: "delete", confirm },
      }),
    onSuccess: invalidate,
  })
}

export type BulkAction =
  | "restore"
  | "setStatus"
  | "setCategory"
  | "addTag"
  | "removeTag"
  | "archive"
  | "refetch"

export function useBulkSites() {
  const invalidate = useInvalidate()
  const queryClient = useQueryClient()
  return useMutation({
    mutationFn: (body: {
      ids: number[]
      action: BulkAction
      value?: unknown
    }) =>
      api<{ updated: number; jobs: Job[] }>("/sites/bulk", {
        method: "POST",
        body,
      }),
    onSuccess: () => {
      invalidate()
      queryClient.invalidateQueries({ queryKey: ["tags"] })
      queryClient.invalidateQueries({ queryKey: ["jobs"] })
    },
  })
}

export function useFetchSite(id: number) {
  const queryClient = useQueryClient()
  return useMutation({
    mutationFn: () =>
      api<{ job: Job }>(`/sites/${id}/fetch`, { method: "POST" }),
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: ["sites", "detail", id] })
      queryClient.invalidateQueries({ queryKey: ["jobs"] })
    },
  })
}

// ---- snapshots --------------------------------------------------------------------------------------------------

export function useSnapshots(siteId: number) {
  return useQuery({
    queryKey: ["snapshots", siteId],
    queryFn: ({ signal }) =>
      api<{ items: SnapshotMeta[]; total: number }>(
        `/sites/${siteId}/snapshots`,
        { signal }
      ),
  })
}

export function useSnapshot(id: number | null | undefined, raw = false) {
  return useQuery({
    queryKey: ["snapshot", id, raw],
    enabled: !!id,
    queryFn: ({ signal }) =>
      api<Snapshot>(`/snapshots/${id}`, {
        query: raw ? { raw: 1 } : undefined,
        signal,
      }),
  })
}

export function useSnapshotDiff(id: number | null, against?: number | null) {
  return useQuery({
    queryKey: ["snapshot-diff", id, against ?? null],
    enabled: !!id,
    queryFn: ({ signal }) =>
      api<SnapshotDiffResult>(`/snapshots/${id}/diff`, {
        query: { against: against ?? undefined },
        signal,
      }),
  })
}

export function useDeleteSnapshot(siteId: number) {
  const queryClient = useQueryClient()
  return useMutation({
    mutationFn: (id: number) =>
      api<void>(`/snapshots/${id}`, { method: "DELETE" }),
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: ["snapshots", siteId] })
      queryClient.invalidateQueries({ queryKey: ["sites"] })
      queryClient.invalidateQueries({ queryKey: ["snapshot"] })
    },
  })
}

// ---- taxonomy and saved views ------------------------------------------------------------------------------------

export function useCategories() {
  return useQuery({
    queryKey: ["categories"],
    queryFn: ({ signal }) =>
      api<{ items: Category[] }>("/categories", { signal }).then(
        (r) => r.items
      ),
  })
}

export function useTags() {
  return useQuery({
    queryKey: ["tags"],
    queryFn: ({ signal }) =>
      api<{ items: Tag[] }>("/tags", { signal }).then((r) => r.items),
  })
}

export function useViews() {
  return useQuery({
    queryKey: ["views"],
    queryFn: ({ signal }) =>
      api<{ items: SavedView[] }>("/views", { signal }).then((r) => r.items),
  })
}

type Taxonomy = "categories" | "tags" | "views"

export function useTaxonomyMutations(kind: Taxonomy) {
  const queryClient = useQueryClient()
  const refresh = () => {
    queryClient.invalidateQueries({ queryKey: [kind] })
    queryClient.invalidateQueries({ queryKey: ["sites"] })
  }
  return {
    create: useMutation({
      mutationFn: (body: Record<string, unknown>) =>
        api(`/${kind}`, { method: "POST", body }),
      onSuccess: refresh,
    }),
    update: useMutation({
      mutationFn: ({ id, ...body }: { id: number } & Record<string, unknown>) =>
        api(`/${kind}/${id}`, { method: "PATCH", body }),
      onSuccess: refresh,
    }),
    remove: useMutation({
      mutationFn: (id: number) =>
        api<void>(`/${kind}/${id}`, { method: "DELETE" }),
      onSuccess: refresh,
    }),
    merge: useMutation({
      mutationFn: ({ id, intoId }: { id: number; intoId: number }) =>
        api(`/${kind}/${id}/merge`, { method: "POST", body: { intoId } }),
      onSuccess: refresh,
    }),
  }
}
