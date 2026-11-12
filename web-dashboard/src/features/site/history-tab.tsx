import { getRouteApi } from "@tanstack/react-router"
import { ArrowRightIcon, GitCompareIcon, TrashIcon } from "lucide-react"
import { useState } from "react"
import { toast } from "sonner"
import { cn } from "cn"

import {
  useDeleteSnapshot,
  useSnapshotDiff,
  useSnapshots,
} from "@/api/hooks/sites"
import type { DiffChange, SnapshotMeta } from "@/api/types"
import { OutcomeBadge } from "@/components/common/badges"
import { EmptyState } from "@/components/common/misc"
import { SimpleSelect } from "@/components/common/simple-select"
import { Button } from "@/components/ui/button"
import { relativeTime, shortDate } from "@/lib/format"

const routeApi = getRouteApi("/sites/$siteId")

function ChangeRow({ change }: { change: DiffChange }) {
  const marker = { added: "+", removed: "−", changed: "~" }[change.kind]
  const tone = {
    added: "text-emerald-700 dark:text-emerald-300",
    removed: "text-rose-700 dark:text-rose-300",
    changed: "text-amber-700 dark:text-amber-300",
  }[change.kind]
  return (
    <li className="flex flex-wrap items-center gap-2 py-1.5 text-sm">
      <span className={cn("w-4 font-mono font-semibold", tone)}>{marker}</span>
      <span>{change.label}</span>
      {change.kind === "changed" ? (
        <span className="flex min-w-0 flex-wrap items-center gap-1.5 font-mono text-xs">
          <span className="text-muted-foreground line-through">
            {change.before ?? "—"}
          </span>
          <ArrowRightIcon className="size-3 text-muted-foreground" />
          <span className="break-all">{change.after ?? "—"}</span>
        </span>
      ) : (
        <span className="font-mono text-xs break-all text-muted-foreground">
          {change.kind === "added" ? change.after : change.before}
        </span>
      )}
    </li>
  )
}

const label = (snapshot: SnapshotMeta) =>
  `${shortDate(snapshot.fetchedAt)} · ${snapshot.outcome}`

export function HistoryTab() {
  const { siteId } = routeApi.useParams()
  const { data, error, isLoading } = useSnapshots(siteId)
  const remove = useDeleteSnapshot(siteId)
  const snapshots = data?.items ?? []
  // null = use the defaults: newest snapshot against the one before it.
  const [bId, setB] = useState<string | null>(null)
  const [aId, setA] = useState<string | null>(null)
  const newest =
    snapshots.find((snapshot) => snapshot.outcome !== "failed") ?? snapshots[0]
  const b = bId ? Number(bId) : (newest?.id ?? null)
  const a = aId ? Number(aId) : null
  const diff = useSnapshotDiff(b, a)

  if (isLoading)
    return <div className="h-40 animate-pulse rounded-xl bg-muted/40" />
  if (error) return <p className="text-sm text-destructive">{error.message}</p>
  if (snapshots.length === 0) {
    return (
      <EmptyState
        icon={<GitCompareIcon />}
        title="No snapshots yet"
        description="Each profile fetch is kept here so you can compare."
      />
    )
  }

  const options = snapshots.map((snapshot) => ({
    value: String(snapshot.id),
    label: label(snapshot),
  }))
  const result = diff.data

  return (
    <div className="grid gap-4 lg:grid-cols-[280px_minmax(0,1fr)]">
      <ul className="flex flex-col gap-1">
        {snapshots.map((snapshot) => (
          <li
            key={snapshot.id}
            className="rounded-lg border border-border bg-card/60 px-3 py-2 text-sm"
          >
            <div className="flex items-center justify-between gap-2">
              <button
                className="text-left hover:underline"
                onClick={() => {
                  setB(String(snapshot.id))
                  setA(null)
                }}
                title="Compare this snapshot with the one before it"
              >
                {shortDate(snapshot.fetchedAt)}
                <span className="ml-1 text-xs text-muted-foreground">
                  {relativeTime(snapshot.fetchedAt)}
                </span>
              </button>
              <OutcomeBadge outcome={snapshot.outcome} />
            </div>
            <div className="flex items-center justify-between text-xs text-muted-foreground">
              <span>
                {snapshot.durationMs === null
                  ? ""
                  : `${Math.round(snapshot.durationMs / 1000)} s · `}
                {snapshot.outcome === "failed"
                  ? "failed"
                  : snapshot.changesCount === null
                    ? "first snapshot"
                    : `${snapshot.changesCount} change${snapshot.changesCount === 1 ? "" : "s"}`}
              </span>
              <Button
                variant="ghost"
                size="icon-xs"
                aria-label="Delete snapshot"
                disabled={remove.isPending}
                onClick={() => {
                  if (
                    window.confirm(
                      `Delete the snapshot from ${shortDate(snapshot.fetchedAt)}?`
                    )
                  )
                    remove.mutate(snapshot.id, {
                      onError: (e) => toast.error(e.message),
                    })
                }}
              >
                <TrashIcon />
              </Button>
            </div>
            {snapshot.errorSummary && snapshot.outcome !== "ok" && (
              <p className="mt-1 line-clamp-2 text-xs text-muted-foreground">
                {snapshot.errorSummary}
              </p>
            )}
          </li>
        ))}
      </ul>

      <div className="rounded-xl border border-border bg-card/60 p-4">
        <div className="mb-4 flex flex-wrap items-center gap-2 text-sm">
          <span className="text-muted-foreground">Compare</span>
          <SimpleSelect
            size="sm"
            value={result?.a ? String(result.a.id) : (aId ?? null)}
            onChange={setA}
            options={options}
            placeholder="Earlier snapshot"
          />
          <span className="text-muted-foreground">with</span>
          <SimpleSelect
            size="sm"
            value={String(b)}
            onChange={(value) => setB(value)}
            options={options}
          />
        </div>
        {diff.isLoading && (
          <div className="h-24 animate-pulse rounded-lg bg-muted/40" />
        )}
        {diff.error && (
          <p className="text-sm text-destructive">{diff.error.message}</p>
        )}
        {result && !result.a && (
          <p className="text-sm text-muted-foreground">
            There is no earlier successful snapshot to compare with yet. Fetch
            the profile again later to see what changed.
          </p>
        )}
        {result?.a && (
          <div className="flex flex-col gap-4">
            {result.count === 0 && (
              <p className="text-sm text-muted-foreground">
                No changes between these two snapshots.
              </p>
            )}
            {result.groups.map((group) => (
              <section key={group.section}>
                <h3 className="mb-1 text-xs font-medium tracking-wide text-muted-foreground uppercase">
                  {group.section}
                </h3>
                <ul className="divide-y divide-border">
                  {group.changes.map((change, index) => (
                    <ChangeRow
                      key={`${change.path}-${index}`}
                      change={change}
                    />
                  ))}
                </ul>
              </section>
            ))}
            {result.count > 0 && result.unchangedSections.length > 0 && (
              <p className="text-xs text-muted-foreground">
                Unchanged: {result.unchangedSections.join(", ")}.
              </p>
            )}
          </div>
        )}
      </div>
    </div>
  )
}
