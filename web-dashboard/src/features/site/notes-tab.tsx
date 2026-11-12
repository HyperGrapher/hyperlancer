import { getRouteApi } from "@tanstack/react-router"
import {
  NotebookPenIcon,
  PinIcon,
  PlusIcon,
  SearchIcon,
  TrashIcon,
} from "lucide-react"
import {
  useCallback,
  useEffect,
  useRef,
  useState,
  type FormEvent,
  type KeyboardEvent,
} from "react"
import { toast } from "sonner"
import { cn } from "cn"

import { ApiError } from "@/api/client"
import {
  useCreateNote,
  useNote,
  useNoteMutations,
  useNotes,
} from "@/api/hooks/work"
import type { Note } from "@/api/types"
import { Markdown } from "@/components/common/markdown"
import { EmptyState } from "@/components/common/misc"
import { Button } from "@/components/ui/button"
import { Input } from "@/components/ui/input"
import { Textarea } from "@/components/ui/textarea"
import { relativeTime } from "@/lib/format"
import { continueList, indent } from "@/lib/markdown-edit"

const routeApi = getRouteApi("/sites/$siteId")

type Mode = "edit" | "split" | "preview"
type SaveState = "saved" | "dirty" | "saving" | "conflict" | "error"

function useDebounced<T>(value: T, delay: number): T {
  const [debounced, setDebounced] = useState(value)
  useEffect(() => {
    const timer = setTimeout(() => setDebounced(value), delay)
    return () => clearTimeout(timer)
  }, [value, delay])
  return debounced
}

const AUTOSAVE_MS = 1500

/** Edits one note. Mount with key={note.id}: the text lives here while the note is open. */
function Editor({
  note,
  siteId,
  onDeleted,
}: {
  note: Note
  siteId: number
  onDeleted: () => void
}) {
  const { save, remove } = useNoteMutations(siteId)
  const [title, setTitle] = useState(note.title)
  const [body, setBody] = useState(note.bodyMd)
  const [mode, setMode] = useState<Mode>(() =>
    note.bodyMd.trim() ? "preview" : "edit"
  )
  const [state, setState] = useState<SaveState>("saved")
  const [conflict, setConflict] = useState<Note | null>(null)
  const textarea = useRef<HTMLTextAreaElement>(null)

  // What the server has, and the timestamp the next save must present.
  const saved = useRef({ title: note.title, body: note.bodyMd })
  const base = useRef(note.updatedAt)
  const latest = useRef({ title, body })
  const inFlight = useRef(false)
  const [revision, setRevision] = useState(0)

  useEffect(() => {
    latest.current = { title, body }
  }, [title, body])

  const dirty = () =>
    latest.current.title !== saved.current.title ||
    latest.current.body !== saved.current.body

  const flush = useCallback(
    (overwriteWith?: string) => {
      if (
        inFlight.current ||
        !latest.current.title.trim() ||
        (!dirty() && !overwriteWith)
      )
        return
      const sent = { ...latest.current }
      inFlight.current = true
      setState("saving")
      save.mutate(
        {
          id: note.id,
          title: sent.title,
          bodyMd: sent.body,
          updatedAt: overwriteWith ?? base.current,
        },
        {
          onSuccess: (result) => {
            inFlight.current = false
            saved.current = sent
            base.current = result.updatedAt
            setConflict(null)
            setState(dirty() ? "dirty" : "saved")
            setRevision((n) => n + 1) // text changed while saving: the effect below saves again
          },
          onError: (error) => {
            inFlight.current = false
            if (error instanceof ApiError && error.status === 409) {
              setConflict((error.details.current as Note) ?? null)
              setState("conflict")
            } else {
              setState("error")
              toast.error(error.message)
            }
          },
        }
      )
    },
    // eslint-disable-next-line react-hooks/exhaustive-deps
    [note.id, save.mutate]
  )

  // Autosave after a pause in typing.
  useEffect(() => {
    if (state === "conflict" || !dirty()) return
    const timer = setTimeout(() => flush(), AUTOSAVE_MS)
    return () => clearTimeout(timer)
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [title, body, revision, state === "conflict"])

  // Unsaved-changes guard: closing the tab warns, switching notes or tabs saves first.
  useEffect(() => {
    const warn = (event: BeforeUnloadEvent) => {
      if (dirty() || inFlight.current) event.preventDefault()
    }
    window.addEventListener("beforeunload", warn)
    return () => {
      window.removeEventListener("beforeunload", warn)
      if (dirty() && latest.current.title.trim()) {
        // Unmounting (another note or tab): send the last edit, ignoring the answer.
        save.mutate({
          id: note.id,
          title: latest.current.title,
          bodyMd: latest.current.body,
          updatedAt: base.current,
        })
      }
    }
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [])

  function onKeyDown(event: KeyboardEvent<HTMLTextAreaElement>) {
    const el = event.currentTarget
    const apply = (edit: { value: string; caret: number } | null) => {
      if (!edit) return false
      event.preventDefault()
      setBody(edit.value)
      setState("dirty")
      requestAnimationFrame(() => el.setSelectionRange(edit.caret, edit.caret))
      return true
    }
    if ((event.ctrlKey || event.metaKey) && event.key.toLowerCase() === "s") {
      event.preventDefault()
      flush()
    } else if (event.key === "Tab" && !event.shiftKey) {
      apply(indent(el.value, el.selectionStart, el.selectionEnd))
    } else if (
      event.key === "Enter" &&
      !event.shiftKey &&
      el.selectionStart === el.selectionEnd
    ) {
      apply(continueList(el.value, el.selectionStart))
    }
  }

  const label = {
    saved: "Saved",
    dirty: "Unsaved changes…",
    saving: "Saving…",
    conflict: "Conflict",
    error: "Not saved",
  }[state]

  return (
    <div className="flex min-w-0 flex-col gap-3">
      <div className="flex flex-wrap items-center justify-between gap-2">
        <Input
          value={title}
          onChange={(event) => {
            setTitle(event.target.value)
            setState("dirty")
          }}
          maxLength={200}
          aria-label="Note title"
          className="h-9 max-w-md text-base font-semibold"
        />
        <div className="flex items-center gap-2">
          <Button
            variant="ghost"
            size="icon-sm"
            aria-label={note.pinned ? "Unpin note" : "Pin note"}
            aria-pressed={note.pinned}
            onClick={() => save.mutate({ id: note.id, pinned: !note.pinned })}
          >
            <PinIcon className={cn(note.pinned && "fill-current")} />
          </Button>
          <Button
            variant="ghost"
            size="icon-sm"
            aria-label="Delete note"
            onClick={() => {
              if (window.confirm(`Delete the note “${note.title}”?`)) {
                saved.current = latest.current // nothing left to save on unmount
                remove.mutate(note.id, {
                  onSuccess: onDeleted,
                  onError: (e) => toast.error(e.message),
                })
              }
            }}
          >
            <TrashIcon />
          </Button>
          <div className="flex rounded-lg bg-muted p-0.5 text-sm">
            {(["edit", "split", "preview"] as const).map((value) => (
              <button
                key={value}
                onClick={() => setMode(value)}
                className={cn(
                  "rounded-md px-2.5 py-0.5 capitalize",
                  mode === value
                    ? "bg-background font-medium shadow-sm"
                    : "text-muted-foreground"
                )}
              >
                {value}
              </button>
            ))}
          </div>
        </div>
      </div>

      {conflict && (
        <div className="flex flex-wrap items-center gap-3 rounded-lg border border-amber-500/30 bg-amber-500/5 px-3 py-2 text-sm">
          <span className="flex-1">
            This note was changed somewhere else since you opened it.
          </span>
          <Button
            size="sm"
            variant="outline"
            onClick={() => {
              setTitle(conflict.title)
              setBody(conflict.bodyMd)
              saved.current = { title: conflict.title, body: conflict.bodyMd }
              base.current = conflict.updatedAt
              setConflict(null)
              setState("saved")
            }}
          >
            Load their version
          </Button>
          <Button size="sm" onClick={() => flush(conflict.updatedAt)}>
            Keep mine
          </Button>
        </div>
      )}

      <div className={cn("grid gap-3", mode === "split" && "md:grid-cols-2")}>
        {mode !== "preview" && (
          <Textarea
            ref={textarea}
            value={body}
            onChange={(event) => {
              setBody(event.target.value)
              setState("dirty")
            }}
            onKeyDown={onKeyDown}
            spellCheck
            aria-label="Note text (Markdown)"
            className="min-h-[420px] font-mono text-sm"
          />
        )}
        {mode !== "edit" && (
          <div className="min-h-[420px] overflow-auto rounded-lg border border-border bg-card/40 p-4">
            {body.trim() ? (
              <Markdown>{body}</Markdown>
            ) : (
              <p className="text-sm text-muted-foreground">
                Nothing to preview yet.
              </p>
            )}
          </div>
        )}
      </div>
      <div className="flex items-start justify-between gap-4">
        <p className="text-xs text-muted-foreground">
          Markdown with tables and task lists. Ctrl+S saves, Tab indents,
          changes save by themselves.
        </p>
        {/* Fixed width: the label changes while typing and must not move anything else. */}
        <span
          className={cn(
            "w-32 shrink-0 text-right text-xs",
            state === "conflict" || state === "error"
              ? "text-destructive"
              : "text-muted-foreground"
          )}
          aria-live="polite"
        >
          {label}
        </span>
      </div>
    </div>
  )
}

function ActiveNote({
  id,
  siteId,
  onDeleted,
}: {
  id: number
  siteId: number
  onDeleted: () => void
}) {
  const { data: note, error } = useNote(id)
  if (error) return <p className="text-sm text-destructive">{error.message}</p>
  if (!note)
    return <div className="h-64 animate-pulse rounded-xl bg-muted/40" />
  return (
    <Editor key={note.id} note={note} siteId={siteId} onDeleted={onDeleted} />
  )
}

export function NotesTab() {
  const { siteId } = routeApi.useParams()
  const [filter, setFilter] = useState("")
  const q = useDebounced(filter.trim(), 300)
  const { data: notes = [], isLoading, error } = useNotes(siteId, q)
  const create = useCreateNote(siteId)
  const [chosen, setChosen] = useState<number | null>(null)
  const [adding, setAdding] = useState(false)
  const [newTitle, setNewTitle] = useState("")

  const activeId =
    chosen !== null && (notes.some((n) => n.id === chosen) || !q)
      ? chosen
      : (notes[0]?.id ?? null)

  function add(event: FormEvent) {
    event.preventDefault()
    if (!newTitle.trim()) return
    create.mutate(
      { title: newTitle.trim() },
      {
        onSuccess: (note) => {
          setChosen(note.id)
          setNewTitle("")
          setAdding(false)
          setFilter("")
        },
        onError: (e) => toast.error(e.message),
      }
    )
  }

  const newNote = adding ? (
    <form onSubmit={add} className="flex gap-1">
      <Input
        autoFocus
        value={newTitle}
        onChange={(e) => setNewTitle(e.target.value)}
        placeholder="Note title"
        maxLength={200}
        onBlur={() => !newTitle && setAdding(false)}
      />
      <Button
        type="submit"
        size="sm"
        disabled={!newTitle.trim() || create.isPending}
      >
        Add
      </Button>
    </form>
  ) : (
    <Button
      variant="outline"
      className="justify-start"
      onClick={() => setAdding(true)}
    >
      <PlusIcon /> New note
    </Button>
  )

  if (isLoading)
    return <div className="h-40 animate-pulse rounded-xl bg-muted/40" />
  if (error) return <p className="text-sm text-destructive">{error.message}</p>

  if (notes.length === 0 && !q) {
    return (
      <EmptyState
        icon={<NotebookPenIcon />}
        title="No notes yet"
        description="Keep meeting notes, briefs and decisions here, in Markdown."
        action={<div className="w-64">{newNote}</div>}
      />
    )
  }

  return (
    <div className="grid gap-4 lg:grid-cols-[240px_minmax(0,1fr)]">
      <div className="flex flex-col gap-2">
        {newNote}
        <div className="relative">
          <SearchIcon className="pointer-events-none absolute top-1/2 left-2.5 size-4 -translate-y-1/2 text-muted-foreground" />
          <Input
            value={filter}
            onChange={(event) => setFilter(event.target.value)}
            placeholder="Search notes"
            className="pl-8"
            aria-label="Search notes"
          />
        </div>
        <ul className="flex flex-col gap-0.5">
          {notes.length === 0 && (
            <li className="px-2 py-1 text-sm text-muted-foreground">
              No notes match.
            </li>
          )}
          {notes.map((note) => (
            <li key={note.id}>
              <button
                onClick={() => setChosen(note.id)}
                className={cn(
                  "flex w-full flex-col rounded-lg px-2.5 py-2 text-left text-sm hover:bg-muted/60",
                  note.id === activeId && "bg-muted"
                )}
              >
                <span className="flex items-center gap-2">
                  {note.pinned && (
                    <PinIcon className="size-3.5 shrink-0 text-muted-foreground" />
                  )}
                  <span className="flex-1 truncate font-medium">
                    {note.title}
                  </span>
                  <span className="shrink-0 text-xs text-muted-foreground">
                    {relativeTime(note.updatedAt)}
                  </span>
                </span>
                {note.excerpt && (
                  <span className="mt-0.5 line-clamp-1 text-xs text-muted-foreground">
                    {note.excerpt.replace(/[#>*`_-]/g, "")}
                  </span>
                )}
              </button>
            </li>
          ))}
        </ul>
      </div>
      {activeId !== null ? (
        <ActiveNote
          id={activeId}
          siteId={siteId}
          onDeleted={() => setChosen(null)}
        />
      ) : (
        <p className="text-sm text-muted-foreground">Select a note.</p>
      )}
    </div>
  )
}
