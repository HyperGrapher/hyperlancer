import { useState, type ReactNode } from "react"
import {
  CircleCheckIcon,
  CircleXIcon,
  DatabaseIcon,
  MergeIcon,
  PencilIcon,
  TrashIcon,
  PlusIcon,
} from "lucide-react"
import { toast } from "sonner"

import {
  useBackups,
  useCancelJob,
  useCreateBackup,
  useHealth,
  useJob,
  useStartSpawnTest,
} from "@/api/hooks/system"
import type { Health } from "@/api/types"
import { CopyButton, PageHeader } from "@/components/common/misc"
import { Button, buttonVariants } from "@/components/ui/button"
import { formatBytes, relativeTime } from "@/lib/format"
import { Input } from "@/components/ui/input"
import { useCategories, useTags, useTaxonomyMutations } from "@/api/hooks/sites"

interface HealthRow {
  label: string
  value: string
  ok: boolean
  copy?: boolean
}

function healthRows(h: Health): HealthRow[] {
  const missing = Object.entries(h.env.extensions)
    .filter(([, ok]) => !ok)
    .map(([name]) => name)
  return [
    { label: "PHP", value: `${h.env.php} (${h.env.phpSapi})`, ok: true },
    {
      label: "Extensions",
      value: missing.length
        ? `missing: ${missing.join(", ")}`
        : Object.keys(h.env.extensions).join(", "),
      ok: missing.length === 0,
    },
    {
      label: "SQLite",
      value: `${h.env.sqlite} · FTS5 ${h.env.fts5 ? "available" : "missing"}`,
      ok: h.env.fts5,
    },
    {
      label: "Data directory",
      value: h.env.dataDir + (h.env.dataDirWritable ? "" : " (not writable)"),
      ok: h.env.dataDirWritable,
      copy: true,
    },
    {
      label: "Migrations",
      value: `${h.migrations.current} of ${h.migrations.latest} applied`,
      ok: h.migrations.current === h.migrations.latest,
    },
    {
      label: "Python",
      value: h.env.python.version
        ? `${h.env.python.path} (${h.env.python.version})`
        : `${h.env.python.path}: not found. Run: uv sync --project scripts`,
      ok: h.env.python.ok,
      copy: true,
    },
    {
      label: "PHP CLI",
      value: h.env.phpCli.ok
        ? h.env.phpCli.path
        : `${h.env.phpCli.path}: not found. Set APP_PHP_CLI or php_cli in config.local.php`,
      ok: h.env.phpCli.ok,
      copy: true,
    },
    {
      label: "Worker",
      value: h.worker.idle
        ? `idle (starts on demand), ${h.worker.queued} queued`
        : "running",
      ok: true,
    },
    ...(h.legacyData.length
      ? [
          {
            label: "Legacy data",
            value: `Old database found: ${h.legacyData.join(", ")}. Run: php backend/bin/migrate.php --import-legacy`,
            ok: false,
          },
        ]
      : []),
  ]
}

function Card({
  title,
  children,
  action,
}: {
  title: string
  children: ReactNode
  action?: ReactNode
}) {
  return (
    <section className="rounded-xl border border-border bg-card/60 p-4">
      <div className="mb-3 flex items-center justify-between">
        <h2 className="text-sm font-medium">{title}</h2>
        {action}
      </div>
      {children}
    </section>
  )
}

function SpawnTest() {
  const [jobId, setJobId] = useState<number | null>(null)
  const start = useStartSpawnTest()
  const cancel = useCancelJob()
  const job = useJob(jobId).data
  const active = job?.status === "queued" || job?.status === "running"

  return (
    <div className="mt-4 rounded-lg border border-border p-3 text-sm">
      <div className="flex items-center justify-between gap-2">
        <div>
          <div className="font-medium">Worker spawn test</div>
          <div className="text-xs text-muted-foreground">
            Starts a 3-minute background job. It must survive page loads and can
            be canceled.
          </div>
        </div>
        {active && job ? (
          <Button
            size="sm"
            variant="outline"
            disabled={cancel.isPending || job.cancelRequested}
            onClick={() => cancel.mutate(job.id)}
          >
            {job.cancelRequested ? "Canceling…" : "Cancel"}
          </Button>
        ) : (
          <Button
            size="sm"
            variant="outline"
            disabled={start.isPending}
            onClick={() =>
              start.mutate(180, {
                onSuccess: ({ job }) => setJobId(job.id),
                onError: (error) => toast.error(error.message),
              })
            }
          >
            Run test
          </Button>
        )}
      </div>
      {job && (
        <div
          className="mt-2 font-mono text-xs text-muted-foreground"
          aria-live="polite"
        >
          job #{job.id} · {job.status}
          {job.progress.current !== null &&
            ` · ${job.progress.current}/${job.progress.total}s`}
          {job.error && ` · ${job.error}`}
        </div>
      )}
    </div>
  )
}

function HealthCard() {
  const { data, error, isLoading, refetch, isFetching } = useHealth()
  return (
    <Card
      title="Environment health"
      action={
        <Button
          size="sm"
          variant="outline"
          disabled={isFetching}
          onClick={() => refetch()}
        >
          Run checks
        </Button>
      }
    >
      {isLoading && <p className="text-sm text-muted-foreground">Checking…</p>}
      {error && (
        <p className="text-sm text-destructive">
          {error.message}
          {error.name === "ApiError" && (error as { logRef?: string }).logRef
            ? ` (log ref ${(error as { logRef?: string }).logRef})`
            : ""}
        </p>
      )}
      {data && (
        <>
          <ul className="flex flex-col gap-2 text-sm">
            {healthRows(data).map((item) => (
              <li key={item.label} className="flex items-start gap-2">
                {item.ok ? (
                  <CircleCheckIcon className="mt-0.5 size-4 shrink-0 text-emerald-500" />
                ) : (
                  <CircleXIcon className="mt-0.5 size-4 shrink-0 text-amber-500" />
                )}
                <span className="w-36 shrink-0 text-muted-foreground">
                  {item.label}
                </span>
                <span className="min-w-0 flex-1 font-mono text-xs break-all">
                  {item.value}
                </span>
                {item.copy && <CopyButton value={item.value} />}
              </li>
            ))}
          </ul>
        </>
      )}
      <SpawnTest />
    </Card>
  )
}

function BackupsCard() {
  const { data, error } = useBackups()
  const create = useCreateBackup()
  return (
    <Card
      title="Backups"
      action={
        <Button
          size="sm"
          disabled={create.isPending}
          onClick={() =>
            create.mutate(undefined, {
              onSuccess: (info) =>
                toast.success(`Backup created: ${info.file}`),
              onError: (e) => toast.error(e.message),
            })
          }
        >
          <DatabaseIcon /> Back up now
        </Button>
      }
    >
      {error && <p className="text-sm text-destructive">{error.message}</p>}
      {data && data.length === 0 && (
        <p className="text-sm text-muted-foreground">No backups yet.</p>
      )}
      <ul className="flex flex-col gap-1.5 text-sm">
        {data?.map((backup) => (
          <li
            key={backup.file}
            className="flex items-center justify-between gap-2 rounded-lg border border-border px-3 py-1.5"
          >
            <span className="min-w-0 truncate font-mono text-xs">
              {backup.file}
            </span>
            <span className="shrink-0 text-xs text-muted-foreground">
              {formatBytes(backup.bytes)} · {relativeTime(backup.createdAt)}
            </span>
            <a
              href={`/api/backups/${encodeURIComponent(backup.file)}/download`}
              className={buttonVariants({ variant: "ghost", size: "xs" })}
              download
            >
              Download
            </a>
          </li>
        ))}
      </ul>
    </Card>
  )
}

function TaxonomyCard({ kind }: { kind: "categories" | "tags" }) {
  const categories = useCategories()
  const tags = useTags()
  const query = kind === "categories" ? categories : tags
  const items = query.data ?? []
  const { create, update, remove, merge } = useTaxonomyMutations(kind)
  const [name, setName] = useState("")
  const singular = kind === "categories" ? "category" : "tag"
  const fail = (e: Error) => toast.error(e.message)

  return (
    <Card title={kind === "categories" ? "Categories" : "Tags"}>
      <form
        className="mb-3 flex gap-2"
        onSubmit={(event) => {
          event.preventDefault()
          if (name.trim())
            create.mutate(
              { name: name.trim() },
              { onSuccess: () => setName(""), onError: fail }
            )
        }}
      >
        <Input
          value={name}
          onChange={(event) => setName(event.target.value)}
          placeholder={`New ${singular}`}
          maxLength={kind === "categories" ? 60 : 40}
          className="h-8"
        />
        <Button
          size="sm"
          type="submit"
          disabled={!name.trim() || create.isPending}
        >
          <PlusIcon /> Add
        </Button>
      </form>
      {query.error && (
        <p className="text-sm text-destructive">{query.error.message}</p>
      )}
      {items.length === 0 && !query.isLoading && (
        <p className="text-sm text-muted-foreground">
          {kind === "categories"
            ? "No categories yet. Add one, such as an industry."
            : "No tags yet. Tags are also created when you add them to a site."}
        </p>
      )}
      <ul className="flex flex-col gap-0.5 text-sm">
        {items.map((item) => (
          <li
            key={item.id}
            className="group flex items-center gap-2 rounded-lg px-2 py-1 hover:bg-muted/60"
          >
            <span className="min-w-0 flex-1 truncate">{item.name}</span>
            <span className="text-xs text-muted-foreground">
              {item.siteCount ?? 0} site{item.siteCount === 1 ? "" : "s"}
            </span>
            <Button
              size="icon-xs"
              variant="ghost"
              aria-label={`Rename ${item.name}`}
              onClick={() => {
                const next = window.prompt(`Rename ${singular}`, item.name)
                if (next && next.trim() && next.trim() !== item.name)
                  update.mutate(
                    { id: item.id, name: next.trim() },
                    { onError: fail }
                  )
              }}
            >
              <PencilIcon />
            </Button>
            {kind === "tags" && items.length > 1 && (
              <Button
                size="icon-xs"
                variant="ghost"
                aria-label={`Merge ${item.name} into another tag`}
                title="Merge into another tag"
                onClick={() => {
                  const target = window.prompt(
                    `Merge “${item.name}” into which tag? Type its exact name.`,
                    items.find((other) => other.id !== item.id)?.name
                  )
                  const into = items.find(
                    (other) =>
                      other.name.toLowerCase() === target?.trim().toLowerCase()
                  )
                  if (target && !into) toast.error(`No tag named “${target}”.`)
                  else if (into && into.id !== item.id)
                    merge.mutate(
                      { id: item.id, intoId: into.id },
                      {
                        onSuccess: () =>
                          toast.success(`Merged into ${into.name}`),
                        onError: fail,
                      }
                    )
                }}
              >
                <MergeIcon />
              </Button>
            )}
            <Button
              size="icon-xs"
              variant="ghost"
              aria-label={`Delete ${item.name}`}
              onClick={() => {
                if (
                  window.confirm(
                    `Delete the ${singular} “${item.name}”? Sites keep everything else.`
                  )
                )
                  remove.mutate(item.id, { onError: fail })
              }}
            >
              <TrashIcon />
            </Button>
          </li>
        ))}
      </ul>
    </Card>
  )
}

export function SettingsPage() {
  return (
    <div>
      <PageHeader
        title="Settings"
        description="Environment health, taxonomy and backups."
      />
      <div className="grid gap-4 lg:grid-cols-2">
        <HealthCard />
        <BackupsCard />
        <TaxonomyCard kind="categories" />
        <TaxonomyCard kind="tags" />
      </div>
    </div>
  )
}
