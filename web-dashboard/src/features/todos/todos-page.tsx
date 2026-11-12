import { useState } from "react"
import { ListTodoIcon } from "lucide-react"

import { useSites } from "@/api/hooks/sites"
import { localToday, useAllTodos } from "@/api/hooks/work"
import type { TodoWithSite } from "@/api/types"
import { EmptyState, PageHeader } from "@/components/common/misc"
import { SimpleSelect } from "@/components/common/simple-select"
import { TodoList } from "@/features/todos/todo-list"
import { daysUntil } from "@/lib/format"

const buckets = [
  {
    key: "overdue",
    label: "Overdue",
    test: (days: number | null) => days !== null && days < 0,
  },
  { key: "today", label: "Today", test: (days: number | null) => days === 0 },
  {
    key: "week",
    label: "This week",
    test: (days: number | null) => days !== null && days > 0 && days <= 7,
  },
  {
    key: "later",
    label: "Later",
    test: (days: number | null) => days !== null && days > 7,
  },
  {
    key: "none",
    label: "No date",
    test: (days: number | null) => days === null,
  },
]

export function TodosPage() {
  const [site, setSite] = useState<string | null>("all")
  const [priority, setPriority] = useState<string | null>("all")
  const sites = useSites({ perPage: 200, sort: "domain" }).data?.items ?? []
  const { data, error, isLoading } = useAllTodos({
    status: "open",
    site: site && site !== "all" ? Number(site) : undefined,
    priority: priority && priority !== "all" ? priority : undefined,
  })
  const open = data?.items ?? []
  localToday() // keeps the bucket maths on the user's calendar day, same as the server query

  const grouped = buckets
    .map((bucket) => ({
      ...bucket,
      items: open.filter((todo: TodoWithSite) =>
        bucket.test(daysUntil(todo.dueDate))
      ),
    }))
    .filter((bucket) => bucket.items.length > 0)

  return (
    <div>
      <PageHeader
        title="To-dos"
        description={
          isLoading
            ? "Loading…"
            : `${data?.total ?? 0} open across all projects`
        }
        actions={
          <>
            <SimpleSelect
              size="sm"
              value={site}
              onChange={setSite}
              options={[
                { value: "all", label: "All sites" },
                ...sites.map((item) => ({
                  value: String(item.id),
                  label: item.siteKey,
                })),
              ]}
            />
            <SimpleSelect
              size="sm"
              value={priority}
              onChange={setPriority}
              options={[
                { value: "all", label: "Any priority" },
                { value: "high", label: "High" },
                { value: "normal", label: "Normal" },
                { value: "low", label: "Low" },
              ]}
            />
          </>
        }
      />
      {error && <p className="text-sm text-destructive">{error.message}</p>}
      {!isLoading && grouped.length === 0 && (
        <EmptyState
          icon={<ListTodoIcon />}
          title="Nothing to do"
          description="Add to-dos from a site's To-dos tab."
        />
      )}
      <div className="flex flex-col gap-4">
        {grouped.map((bucket) => (
          <section
            key={bucket.key}
            className="rounded-xl border border-border bg-card/60 px-4 pt-3 pb-1"
          >
            <h2 className="text-xs font-medium tracking-wide text-muted-foreground uppercase">
              {bucket.label} · {bucket.items.length}
            </h2>
            <TodoList todos={bucket.items} showSite />
          </section>
        ))}
      </div>
    </div>
  )
}
