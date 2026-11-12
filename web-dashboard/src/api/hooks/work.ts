import {
  useInfiniteQuery,
  useMutation,
  useQuery,
  useQueryClient,
} from "@tanstack/react-query"

import { api } from "@/api/client"
import type {
  ActivityEntry,
  Contact,
  DashboardData,
  Note,
  NoteMeta,
  Priority,
  SiteLinkItem,
  Todo,
  TodoWithSite,
} from "@/api/types"

/** Local date as YYYY-MM-DD: "today" in the user's calendar, not the server's. */
export function localToday(): string {
  const now = new Date()
  const pad = (n: number) => String(n).padStart(2, "0")
  return `${now.getFullYear()}-${pad(now.getMonth() + 1)}-${pad(now.getDate())}`
}

function useRefresh(...keys: string[][]) {
  const queryClient = useQueryClient()
  return () =>
    Promise.all(
      keys.map((queryKey) => queryClient.invalidateQueries({ queryKey }))
    )
}

// ---- notes ----------------------------------------------------------------------------------------------------------

export function useNotes(siteId: number, q: string) {
  return useQuery({
    queryKey: ["notes", siteId, q],
    queryFn: ({ signal }) =>
      api<{ items: NoteMeta[] }>(`/sites/${siteId}/notes`, {
        query: { q: q || undefined },
        signal,
      }).then((r) => r.items),
  })
}

export function useNote(id: number | null) {
  return useQuery({
    queryKey: ["note", id],
    enabled: id !== null,
    queryFn: ({ signal }) => api<Note>(`/notes/${id}`, { signal }),
    staleTime: Infinity, // the editor owns the text while it is open
  })
}

export function useCreateNote(siteId: number) {
  const refresh = useRefresh(
    ["notes", String(siteId)],
    ["sites", "detail"],
    ["activity"]
  )
  return useMutation({
    mutationFn: (body: { title: string; bodyMd?: string }) =>
      api<Note>(`/sites/${siteId}/notes`, { method: "POST", body }),
    onSuccess: refresh,
  })
}

export function useNoteMutations(siteId: number) {
  const queryClient = useQueryClient()
  const refresh = useRefresh(["notes"], ["sites", "detail"], ["activity"])
  return {
    save: useMutation({
      mutationFn: ({
        id,
        ...body
      }: {
        id: number
        title?: string
        bodyMd?: string
        pinned?: boolean
        updatedAt?: string
      }) => api<Note>(`/notes/${id}`, { method: "PATCH", body }),
      onSuccess: (note) => {
        queryClient.setQueryData(["note", note.id], note)
        queryClient.invalidateQueries({ queryKey: ["notes", siteId] })
        queryClient.invalidateQueries({ queryKey: ["activity"] })
      },
    }),
    remove: useMutation({
      mutationFn: (id: number) =>
        api<void>(`/notes/${id}`, { method: "DELETE" }),
      onSuccess: refresh,
    }),
  }
}

// ---- to-dos ---------------------------------------------------------------------------------------------------------

export function useSiteTodos(siteId: number) {
  return useQuery({
    queryKey: ["todos", "site", siteId],
    queryFn: ({ signal }) =>
      api<{ items: Todo[] }>(`/sites/${siteId}/todos`, { signal }).then(
        (r) => r.items
      ),
  })
}

export function useAllTodos(filters: {
  status?: string
  site?: number
  priority?: string
}) {
  const today = localToday()
  return useQuery({
    queryKey: ["todos", "all", filters, today],
    queryFn: ({ signal }) =>
      api<{ items: TodoWithSite[]; total: number }>("/todos", {
        query: { ...filters, today },
        signal,
      }),
  })
}

export interface NewTodo {
  title: string
  dueDate?: string | null
  priority?: Priority
  detail?: string | null
}

export function useTodoMutations() {
  const refresh = useRefresh(
    ["todos"],
    ["sites", "detail"],
    ["sites", "list"],
    ["dashboard"],
    ["activity"]
  )
  return {
    create: useMutation({
      mutationFn: ({ siteId, ...body }: NewTodo & { siteId: number }) =>
        api<Todo>(`/sites/${siteId}/todos`, { method: "POST", body }),
      onSuccess: refresh,
    }),
    update: useMutation({
      mutationFn: ({
        id,
        ...body
      }: { id: number } & Partial<NewTodo> & {
          done?: boolean
          sortOrder?: number
        }) => api<Todo>(`/todos/${id}`, { method: "PATCH", body }),
      onSuccess: refresh,
    }),
    remove: useMutation({
      mutationFn: (id: number) =>
        api<void>(`/todos/${id}`, { method: "DELETE" }),
      onSuccess: refresh,
    }),
  }
}

// ---- activity -------------------------------------------------------------------------------------------------------

export function useActivity(siteId: number | null) {
  return useInfiniteQuery({
    queryKey: ["activity", siteId],
    initialPageParam: undefined as number | undefined,
    queryFn: ({ pageParam, signal }) =>
      api<{ items: ActivityEntry[]; nextBefore: number | null }>(
        siteId === null ? "/activity" : `/sites/${siteId}/activity`,
        {
          query: { before: pageParam, limit: 30 },
          signal,
        }
      ),
    getNextPageParam: (last) => last.nextBefore ?? undefined,
  })
}

// ---- contacts and links ---------------------------------------------------------------------------------------------

export function useContactMutations(siteId: number) {
  const refresh = useRefresh(["sites", "detail"], ["activity"])
  return {
    create: useMutation({
      mutationFn: (body: Partial<Contact>) =>
        api<Contact>(`/sites/${siteId}/contacts`, { method: "POST", body }),
      onSuccess: refresh,
    }),
    update: useMutation({
      mutationFn: ({ id, ...body }: { id: number } & Partial<Contact>) =>
        api<Contact>(`/contacts/${id}`, { method: "PATCH", body }),
      onSuccess: refresh,
    }),
    remove: useMutation({
      mutationFn: (id: number) =>
        api<void>(`/contacts/${id}`, { method: "DELETE" }),
      onSuccess: refresh,
    }),
  }
}

export function useLinkMutations(siteId: number) {
  const refresh = useRefresh(["sites", "detail"], ["activity"])
  return {
    create: useMutation({
      mutationFn: (body: Partial<SiteLinkItem>) =>
        api<SiteLinkItem>(`/sites/${siteId}/links`, { method: "POST", body }),
      onSuccess: refresh,
    }),
    update: useMutation({
      mutationFn: ({ id, ...body }: { id: number } & Partial<SiteLinkItem>) =>
        api<SiteLinkItem>(`/links/${id}`, { method: "PATCH", body }),
      onSuccess: refresh,
    }),
    remove: useMutation({
      mutationFn: (id: number) =>
        api<void>(`/links/${id}`, { method: "DELETE" }),
      onSuccess: refresh,
    }),
  }
}

// ---- dashboard ------------------------------------------------------------------------------------------------------

export function useDashboard() {
  const today = localToday()
  return useQuery({
    queryKey: ["dashboard", today],
    queryFn: ({ signal }) =>
      api<DashboardData>("/dashboard", { query: { today }, signal }),
    refetchInterval: 15000,
  })
}
