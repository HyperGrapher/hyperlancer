import { getRouteApi } from "@tanstack/react-router"
import { ListTodoIcon } from "lucide-react"
import { useState, type FormEvent } from "react"
import { toast } from "sonner"

import { useSiteTodos, useTodoMutations } from "@/api/hooks/work"

import { EmptyState } from "@/components/common/misc"
import { Input } from "@/components/ui/input"
import { TodoList } from "@/features/todos/todo-list"
import { parseQuickAdd } from "@/lib/quick-add"

const routeApi = getRouteApi("/sites/$siteId")

export function TodosTab() {
  const { siteId } = routeApi.useParams()
  const { data: todos = [], error, isLoading } = useSiteTodos(siteId)
  const { create } = useTodoMutations()
  const [title, setTitle] = useState("")
  const [showDone, setShowDone] = useState(false)

  const open = todos.filter((todo) => !todo.done)
  const finished = todos.filter((todo) => todo.done)
  const preview = title.trim() ? parseQuickAdd(title) : null

  function add(event: FormEvent) {
    event.preventDefault()
    const parsed = parseQuickAdd(title)
    if (!parsed.title) return
    create.mutate(
      {
        siteId,
        title: parsed.title,
        priority: parsed.priority,
        dueDate: parsed.dueDate,
      },
      { onSuccess: () => setTitle(""), onError: (e) => toast.error(e.message) }
    )
  }

  return (
    <div className="flex flex-col gap-3">
      <form onSubmit={add}>
        <Input
          value={title}
          onChange={(event) => setTitle(event.target.value)}
          placeholder="Add a to-do and press Enter… (try “Send invoice !high due:fri”)"
          maxLength={320}
          aria-label="New to-do"
        />
        {preview && (preview.dueDate || preview.priority !== "normal") && (
          <p className="mt-1 text-xs text-muted-foreground">
            → {preview.title || "…"} · priority {preview.priority}
            {preview.dueDate && ` · due ${preview.dueDate}`}
          </p>
        )}
      </form>
      {error && <p className="text-sm text-destructive">{error.message}</p>}
      {isLoading ? (
        <div className="h-24 animate-pulse rounded-xl bg-muted/40" />
      ) : todos.length === 0 ? (
        <EmptyState
          icon={<ListTodoIcon />}
          title="No to-dos for this site"
          description="Type one above. Drag open items to reorder them."
        />
      ) : (
        <div className="rounded-xl border border-border bg-card/60 px-4">
          {open.length > 0 ? (
            <TodoList todos={open} reorderable />
          ) : (
            <p className="py-3 text-sm text-muted-foreground">
              Nothing open. Nice.
            </p>
          )}
          {finished.length > 0 && (
            <div className="border-t border-border py-2">
              <button
                className="text-xs text-muted-foreground hover:text-foreground"
                onClick={() => setShowDone(!showDone)}
              >
                {showDone ? "Hide" : "Show"} {finished.length} done
              </button>
              {showDone && <TodoList todos={finished} />}
            </div>
          )}
        </div>
      )}
    </div>
  )
}
