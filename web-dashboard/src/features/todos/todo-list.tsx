import { Link } from "@tanstack/react-router"
import { ArrowDownIcon, ArrowUpIcon, TrashIcon } from "lucide-react"
import { useState } from "react"
import { toast } from "sonner"
import { cn } from "cn"

import { useTodoMutations } from "@/api/hooks/work"
import type { Priority, Todo, TodoWithSite } from "@/api/types"
import { PriorityLabel } from "@/components/common/badges"
import { SimpleSelect } from "@/components/common/simple-select"
import { Button } from "@/components/ui/button"
import { Checkbox } from "@/components/ui/checkbox"
import {
  Dialog,
  DialogContent,
  DialogFooter,
  DialogHeader,
  DialogTitle,
} from "@/components/ui/dialog"
import { Input } from "@/components/ui/input"
import { Label } from "@/components/ui/label"
import { Textarea } from "@/components/ui/textarea"
import { daysUntil, dueLabel } from "@/lib/format"

/** The fields the list needs: full to-dos fit, and so do the trimmed rows of the dashboard. */
type Item = Pick<Todo, "id" | "title" | "done" | "dueDate" | "priority"> & {
  detail?: string | null
  sortOrder?: number
  site?: TodoWithSite["site"]
}

function EditDialog({ todo, onClose }: { todo: Item; onClose: () => void }) {
  const { update } = useTodoMutations()
  const [title, setTitle] = useState(todo.title)
  const [detail, setDetail] = useState(todo.detail ?? "")
  const [due, setDue] = useState(todo.dueDate ?? "")
  const [priority, setPriority] = useState<string | null>(todo.priority)

  return (
    <Dialog open onOpenChange={(open) => !open && onClose()}>
      <DialogContent>
        <DialogHeader>
          <DialogTitle>Edit to-do</DialogTitle>
        </DialogHeader>
        <form
          className="grid gap-3"
          onSubmit={(event) => {
            event.preventDefault()
            update.mutate(
              {
                id: todo.id,
                title,
                detail: detail || null,
                dueDate: due || null,
                priority: (priority ?? "normal") as Priority,
              },
              { onSuccess: onClose, onError: (e) => toast.error(e.message) }
            )
          }}
        >
          <div className="grid gap-1.5">
            <Label htmlFor="todo-title">Title</Label>
            <Input
              id="todo-title"
              value={title}
              onChange={(e) => setTitle(e.target.value)}
              maxLength={300}
              autoFocus
            />
          </div>
          <div className="grid grid-cols-2 gap-3">
            <div className="grid gap-1.5">
              <Label htmlFor="todo-due">Due date</Label>
              <Input
                id="todo-due"
                type="date"
                value={due}
                onChange={(e) => setDue(e.target.value)}
              />
            </div>
            <div className="grid gap-1.5">
              <Label>Priority</Label>
              <SimpleSelect
                value={priority}
                onChange={setPriority}
                className="w-full"
                options={[
                  { value: "high", label: "High" },
                  { value: "normal", label: "Normal" },
                  { value: "low", label: "Low" },
                ]}
              />
            </div>
          </div>
          <div className="grid gap-1.5">
            <Label htmlFor="todo-detail">Note</Label>
            <Textarea
              id="todo-detail"
              value={detail}
              onChange={(e) => setDetail(e.target.value)}
              rows={3}
              maxLength={2000}
            />
          </div>
          <DialogFooter>
            <Button type="submit" disabled={!title.trim() || update.isPending}>
              Save
            </Button>
          </DialogFooter>
        </form>
      </DialogContent>
    </Dialog>
  )
}

/**
 * Shared by the site tab, the global page and the dashboard. `reorderable` lets rows be dragged: the new sort order
 * is the midpoint between the two neighbours, so only the moved row is written.
 */
export function TodoList({
  todos,
  showSite = false,
  reorderable = false,
}: {
  todos: Item[]
  showSite?: boolean
  reorderable?: boolean
}) {
  const { update, remove } = useTodoMutations()
  const [editing, setEditing] = useState<Item | null>(null)
  const [dragId, setDragId] = useState<number | null>(null)
  const [overId, setOverId] = useState<number | null>(null)

  /** Moves a row one place by giving it a sort order between its new neighbours. */
  function move(index: number, direction: -1 | 1) {
    const row = todos[index]
    const neighbour = todos[index + direction]
    if (!row || !neighbour) return
    const beyond = todos[index + direction * 2]
    const near = neighbour.sortOrder ?? 0
    const far = beyond ? (beyond.sortOrder ?? 0) : near + direction * 2
    update.mutate(
      { id: row.id, sortOrder: (near + far) / 2 },
      { onError: (e) => toast.error(e.message) }
    )
  }

  function drop(targetId: number) {
    const moved = todos.find((todo) => todo.id === dragId)
    setDragId(null)
    setOverId(null)
    if (!moved || moved.id === targetId) return
    const others = todos.filter((todo) => todo.id !== moved.id)
    const index = others.findIndex((todo) => todo.id === targetId)
    const target = others[index].sortOrder ?? 0
    const before = index > 0 ? (others[index - 1].sortOrder ?? 0) : target - 2
    const sortOrder = (before + target) / 2
    update.mutate(
      { id: moved.id, sortOrder },
      { onError: (e) => toast.error(e.message) }
    )
  }

  return (
    <>
      <ul className="divide-y divide-border">
        {todos.map((todo, index) => {
          const days = daysUntil(todo.dueDate)
          return (
            <li
              key={todo.id}
              draggable={reorderable && !todo.done}
              onDragStart={() => setDragId(todo.id)}
              onDragEnd={() => {
                setDragId(null)
                setOverId(null)
              }}
              onDragOver={(event) => {
                if (dragId !== null) {
                  event.preventDefault()
                  setOverId(todo.id)
                }
              }}
              onDrop={() => drop(todo.id)}
              className={cn(
                "group flex items-center gap-3 py-2",
                reorderable && !todo.done && "cursor-grab",
                overId === todo.id &&
                  dragId !== todo.id &&
                  "border-t-2 border-primary"
              )}
            >
              {reorderable && !todo.done && (
                <div className="flex shrink-0 flex-col">
                  <Button
                    variant="ghost"
                    size="icon-xs"
                    className="h-3.5 w-5 text-muted-foreground"
                    aria-label={`Move “${todo.title}” up`}
                    disabled={index === 0 || update.isPending}
                    onClick={() => move(index, -1)}
                  >
                    <ArrowUpIcon className="size-3" />
                  </Button>
                  <Button
                    variant="ghost"
                    size="icon-xs"
                    className="h-3.5 w-5 text-muted-foreground"
                    aria-label={`Move “${todo.title}” down`}
                    disabled={index === todos.length - 1 || update.isPending}
                    onClick={() => move(index, 1)}
                  >
                    <ArrowDownIcon className="size-3" />
                  </Button>
                </div>
              )}
              <Checkbox
                checked={todo.done}
                onCheckedChange={(checked) =>
                  update.mutate(
                    { id: todo.id, done: checked },
                    { onError: (e) => toast.error(e.message) }
                  )
                }
                aria-label={`Mark “${todo.title}” as ${todo.done ? "open" : "done"}`}
              />
              <button
                type="button"
                onClick={() => setEditing(todo)}
                className={cn(
                  "min-w-0 flex-1 text-left text-sm hover:underline",
                  todo.done && "text-muted-foreground line-through"
                )}
              >
                <span className="block truncate">{todo.title}</span>
                {todo.detail && (
                  <span className="block truncate text-xs text-muted-foreground no-underline">
                    {todo.detail}
                  </span>
                )}
              </button>
              <PriorityLabel priority={todo.priority} />
              {showSite && todo.site && (
                <Link
                  to="/sites/$siteId/todos"
                  params={{ siteId: todo.site.id }}
                  className="hidden text-xs text-muted-foreground hover:text-foreground hover:underline sm:inline"
                >
                  {todo.site.siteKey}
                </Link>
              )}
              <span
                className={cn(
                  "w-24 shrink-0 text-right text-xs",
                  !todo.done && days !== null && days < 0
                    ? "text-rose-600 dark:text-rose-300"
                    : "text-muted-foreground"
                )}
              >
                {dueLabel(todo.dueDate)}
              </span>
              <Button
                variant="ghost"
                size="icon-xs"
                className="opacity-0 group-hover:opacity-100 focus-visible:opacity-100"
                aria-label={`Delete “${todo.title}”`}
                onClick={() =>
                  remove.mutate(todo.id, {
                    onError: (e) => toast.error(e.message),
                  })
                }
              >
                <TrashIcon />
              </Button>
            </li>
          )
        })}
      </ul>
      {editing && (
        <EditDialog todo={editing} onClose={() => setEditing(null)} />
      )}
    </>
  )
}
