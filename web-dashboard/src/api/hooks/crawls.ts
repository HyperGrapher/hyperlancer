import { useMutation, useQuery, useQueryClient } from "@tanstack/react-query"

import { api, apiUrl } from "@/api/client"
import type {
  Crawl,
  CrawlAsset,
  CrawlDetail,
  CrawlPage,
  CrawlPageItem,
  CrawlTreeNode,
  Job,
} from "@/api/types"

export const crawlActive = (crawl: Pick<Crawl, "status">) =>
  crawl.status === "queued" ||
  crawl.status === "running" ||
  crawl.status === "canceling"

export function useCrawls(siteId: number) {
  return useQuery({
    queryKey: ["crawls", siteId],
    queryFn: ({ signal }) =>
      api<{ items: Crawl[] }>(`/sites/${siteId}/crawls`, { signal }).then(
        (r) => r.items
      ),
    refetchInterval: (query) =>
      query.state.data?.some(crawlActive) ? 1000 : false,
  })
}

export function useCrawl(id: number | null) {
  return useQuery({
    queryKey: ["crawl", id],
    enabled: id !== null,
    queryFn: ({ signal }) => api<CrawlDetail>(`/crawls/${id}`, { signal }),
    refetchInterval: (query) =>
      query.state.data && crawlActive(query.state.data) ? 1000 : false,
  })
}

export function useStartCrawl(siteId: number) {
  const queryClient = useQueryClient()
  return useMutation({
    mutationFn: (options: Record<string, unknown>) =>
      api<{ crawl: Crawl }>(`/sites/${siteId}/crawls`, {
        method: "POST",
        body: options,
      }),
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: ["crawls", siteId] })
      queryClient.invalidateQueries({ queryKey: ["sites", "detail", siteId] })
      queryClient.invalidateQueries({ queryKey: ["jobs"] })
    },
  })
}

export function useCancelCrawl(siteId: number) {
  const queryClient = useQueryClient()
  return useMutation({
    mutationFn: (jobId: number) =>
      api<Job>(`/jobs/${jobId}/cancel`, { method: "POST" }),
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: ["crawls", siteId] })
      queryClient.invalidateQueries({ queryKey: ["crawl"] })
    },
  })
}

export function useDeleteCrawl(siteId: number) {
  const queryClient = useQueryClient()
  return useMutation({
    mutationFn: (id: number) =>
      api<void>(`/crawls/${id}`, { method: "DELETE" }),
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: ["crawls", siteId] })
      queryClient.invalidateQueries({ queryKey: ["sites", "detail", siteId] })
    },
  })
}

export function useRevealCrawl() {
  return useMutation({
    mutationFn: (id: number) =>
      api<void>(`/crawls/${id}/reveal`, { method: "POST" }),
  })
}

export function useCrawlPages(id: number, enabled: boolean) {
  return useQuery({
    queryKey: ["crawl", id, "pages"],
    enabled,
    queryFn: ({ signal }) =>
      api<{ items: CrawlPageItem[]; tree: CrawlTreeNode | null }>(
        `/crawls/${id}/pages`,
        { signal }
      ),
  })
}

export function useCrawlPage(id: number, pageId: string | null) {
  return useQuery({
    queryKey: ["crawl", id, "page", pageId],
    enabled: pageId !== null,
    queryFn: ({ signal }) =>
      api<CrawlPage>(`/crawls/${id}/pages/${encodeURIComponent(pageId!)}`, {
        signal,
      }),
    staleTime: Infinity,
  })
}

export function useCrawlAssets(
  id: number,
  kind: "image" | "file",
  enabled: boolean
) {
  return useQuery({
    queryKey: ["crawl", id, "assets", kind],
    enabled,
    queryFn: ({ signal }) =>
      api<{ items: CrawlAsset[]; total: number }>(`/crawls/${id}/assets`, {
        query: { kind, perPage: 200 },
        signal,
      }),
    staleTime: Infinity,
  })
}

/** URL of a file inside a crawl folder, served by the API with a safe content type. */
export const crawlFileUrl = (id: number, path: string, download = false) =>
  apiUrl(`/crawls/${id}/raw`, { path, download: download ? 1 : undefined })

export const crawlArchiveUrl = (id: number) => apiUrl(`/crawls/${id}/archive`)

export function useBuildArchive() {
  const queryClient = useQueryClient()
  return useMutation({
    mutationFn: (id: number) =>
      api<{ ready: boolean; job?: Job }>(`/crawls/${id}/archive`, {
        method: "POST",
      }),
    onSuccess: () => queryClient.invalidateQueries({ queryKey: ["crawl"] }),
  })
}
