import { cn } from "cn"
import {
  getRouteApi,
  Link,
  Outlet,
  useNavigate,
  useRouterState,
} from "@tanstack/react-router"
import {
  ArchiveIcon,
  ArchiveRestoreIcon,
  CalendarDaysIcon,
  ChevronDownIcon,
  DownloadIcon,
  EllipsisIcon,
  ExternalLinkIcon,
  EyeIcon,
  GlobeIcon,
  LoaderCircleIcon,
  PencilIcon,
  PlusIcon,
  RefreshCwIcon,
  ShieldCheckIcon,
  TrashIcon,
  XIcon,
} from "lucide-react"
import { useState, type FormEvent } from "react"
import { toast } from "sonner"

import {
  useArchiveSite,
  useCategories,
  useDeleteSite,
  useFetchSite,
  usePatchSite,
  useRestoreSite,
  useSite,
} from "@/api/hooks/sites"
import type { SiteDetail, SiteStatus } from "@/api/types"
import { StatusBadge } from "@/components/common/badges"
import { CopyButton, EmptyState } from "@/components/common/misc"
import { SiteIcon } from "@/components/common/site-icon"
import { SimpleSelect } from "@/components/common/simple-select"
import { Button, buttonVariants } from "@/components/ui/button"
import { Checkbox } from "@/components/ui/checkbox"
import {
  Dialog,
  DialogContent,
  DialogDescription,
  DialogFooter,
  DialogHeader,
  DialogTitle,
} from "@/components/ui/dialog"
import {
  DropdownMenu,
  DropdownMenuContent,
  DropdownMenuItem,
  DropdownMenuSeparator,
  DropdownMenuTrigger,
} from "@/components/ui/dropdown-menu"
import { Input } from "@/components/ui/input"
import { Label } from "@/components/ui/label"
import {
  Popover,
  PopoverContent,
  PopoverTrigger,
} from "@/components/ui/popover"
import { Progress } from "@/components/ui/progress"
import { daysUntil, dueLabel, shortDate } from "@/lib/format"
import { ContactsSection, LinksSection } from "@/features/site/contacts-links"
import { STATUSES } from "@/lib/domain"

const routeApi = getRouteApi("/sites/$siteId")

const tabs = [
  { to: "/sites/$siteId", label: "Profile", exact: true },
  { to: "/sites/$siteId/notes", label: "Notes", exact: false },
  { to: "/sites/$siteId/todos", label: "To-dos", exact: false },
  { to: "/sites/$siteId/content", label: "Content", exact: false },
  { to: "/sites/$siteId/history", label: "History", exact: false },
  { to: "/sites/$siteId/activity", label: "Activity", exact: false },
] as const

function tabCount(site: SiteDetail, label: string) {
  const { counts } = site
  switch (label) {
    case "Notes":
      return counts.notes
    case "To-dos": {
      const total = counts.todosOpen + counts.todosDone
      return total ? `${counts.todosOpen}/${total}` : 0
    }
    case "Content":
      return counts.crawls
    case "History":
      return counts.snapshots
    default:
      return null
  }
}

function Sidebar({ site }: { site: SiteDetail }) {
  const tls = daysUntil(site.tlsValidUntil)
  const domain = daysUntil(site.domainExpiresOn)

  return (
    <aside className="flex flex-col gap-4 text-sm">
      <section className="rounded-xl border border-border bg-card/60 p-4">
        <h3 className="mb-2 font-medium">Project</h3>
        <div className="flex items-center gap-2 text-muted-foreground">
          <CalendarDaysIcon className="size-4" />
          Deadline:{" "}
          <span className="text-foreground">
            {site.deadline
              ? `${shortDate(site.deadline)} (${dueLabel(site.deadline)})`
              : "none"}
          </span>
        </div>
      </section>

      <ContactsSection site={site} />

      <LinksSection site={site} />

      <section className="rounded-xl border border-border bg-card/60 p-4">
        <h3 className="mb-2 font-medium">Health</h3>
        <div className="flex flex-col gap-1.5">
          <div className="flex items-center justify-between">
            <span className="flex items-center gap-2 text-muted-foreground">
              <ShieldCheckIcon className="size-4" /> SSL
            </span>
            <span
              className={
                tls !== null && tls < 30
                  ? "text-amber-600 dark:text-amber-300"
                  : ""
              }
            >
              {tls === null ? "unknown" : `${tls} days`}
            </span>
          </div>
          <div className="flex items-center justify-between">
            <span className="flex items-center gap-2 text-muted-foreground">
              <GlobeIcon className="size-4" /> Domain
            </span>
            <span
              className={
                domain !== null && domain < 60
                  ? "text-amber-600 dark:text-amber-300"
                  : ""
              }
            >
              {domain === null ? "unknown" : `${domain} days`}
            </span>
          </div>
        </div>
      </section>
    </aside>
  )
}

function EditDialog({
  site,
  open,
  onOpenChange,
}: {
  site: SiteDetail
  open: boolean
  onOpenChange: (open: boolean) => void
}) {
  const patch = usePatchSite(site.id)
  const [name, setName] = useState(site.name ?? "")
  const [url, setUrl] = useState(site.url)
  const [deadline, setDeadline] = useState(site.deadline ?? "")
  const [allowPrivate, setAllowPrivate] = useState(site.allowPrivateNet)

  function submit(event: FormEvent) {
    event.preventDefault()
    patch.mutate(
      {
        name: name.trim() || null,
        url,
        deadline: deadline || null,
        allowPrivateNet: allowPrivate,
      },
      {
        onSuccess: () => {
          toast.success("Saved")
          onOpenChange(false)
        },
        onError: (e) => toast.error(e.message),
      }
    )
  }

  return (
    <Dialog open={open} onOpenChange={onOpenChange}>
      <DialogContent>
        <DialogHeader>
          <DialogTitle>Edit details</DialogTitle>
          <DialogDescription>
            Changing the URL changes the site key too.
          </DialogDescription>
        </DialogHeader>
        <form onSubmit={submit} className="space-y-3">
          <div className="space-y-1.5">
            <Label htmlFor="edit-name">Name</Label>
            <Input
              id="edit-name"
              value={name}
              onChange={(e) => setName(e.target.value)}
              maxLength={120}
            />
          </div>
          <div className="space-y-1.5">
            <Label htmlFor="edit-url">URL</Label>
            <Input
              id="edit-url"
              value={url}
              onChange={(e) => setUrl(e.target.value)}
            />
          </div>
          <div className="space-y-1.5">
            <Label htmlFor="edit-deadline">Deadline</Label>
            <Input
              id="edit-deadline"
              type="date"
              value={deadline}
              onChange={(e) => setDeadline(e.target.value)}
            />
          </div>
          <label className="flex items-center gap-2 text-sm">
            <Checkbox
              checked={allowPrivate}
              onCheckedChange={(v) => setAllowPrivate(!!v)}
            />{" "}
            Allow local/LAN target
          </label>
          <DialogFooter>
            <Button type="submit" disabled={patch.isPending}>
              Save
            </Button>
          </DialogFooter>
        </form>
      </DialogContent>
    </Dialog>
  )
}

function DeleteDialog({
  site,
  open,
  onOpenChange,
}: {
  site: SiteDetail
  open: boolean
  onOpenChange: (open: boolean) => void
}) {
  const remove = useDeleteSite()
  const navigate = useNavigate()
  return (
    <Dialog open={open} onOpenChange={onOpenChange}>
      <DialogContent>
        <DialogHeader>
          <DialogTitle>Delete {site.siteKey}?</DialogTitle>
          <DialogDescription>
            This permanently removes the site with its profile history, notes
            and to-dos. Archiving keeps everything and can be undone.
          </DialogDescription>
        </DialogHeader>
        <DialogFooter>
          <Button variant="outline" onClick={() => onOpenChange(false)}>
            Cancel
          </Button>
          <Button
            variant="destructive"
            disabled={remove.isPending}
            onClick={() =>
              remove.mutate(
                { id: site.id, confirm: site.siteKey },
                {
                  onSuccess: () => {
                    toast.success(`${site.siteKey} deleted`)
                    navigate({ to: "/sites" })
                  },
                  onError: (e) => toast.error(e.message),
                }
              )
            }
          >
            Delete permanently
          </Button>
        </DialogFooter>
      </DialogContent>
    </Dialog>
  )
}

function PreviewLink({ site }: { site: SiteDetail }) {
  const patch = usePatchSite(site.id)
  const [open, setOpen] = useState(false)
  const [value, setValue] = useState(site.previewUrl ?? "")

  function save(next: string | null) {
    patch.mutate(
      { previewUrl: next },
      {
        onSuccess: () => {
          setOpen(false)
          toast.success(next ? "Preview URL saved" : "Preview URL removed")
        },
        onError: (e) => toast.error(e.message),
      }
    )
  }

  return (
    <>
      {site.previewUrl && (
        <a
          href={site.previewUrl}
          target="_blank"
          rel="noopener noreferrer"
          className={buttonVariants({ variant: "outline", size: "xs" })}
          title={site.previewUrl}
        >
          <EyeIcon /> Preview
        </a>
      )}
      <Popover
        open={open}
        onOpenChange={(next) => {
          setOpen(next)
          if (next) setValue(site.previewUrl ?? "")
        }}
      >
        <PopoverTrigger
          className={buttonVariants({
            variant: "ghost",
            size: site.previewUrl ? "icon-xs" : "xs",
          })}
          aria-label={site.previewUrl ? "Edit preview URL" : "Add preview URL"}
        >
          {site.previewUrl ? (
            <PencilIcon />
          ) : (
            <>
              <PlusIcon /> Preview URL
            </>
          )}
        </PopoverTrigger>
        <PopoverContent className="w-80">
          <form
            className="grid gap-2"
            onSubmit={(event) => {
              event.preventDefault()
              save(value.trim() || null)
            }}
          >
            <Label htmlFor="preview-url" className="text-xs">
              Preview URL (for example the Vercel deployment of the redesign)
            </Label>
            <Input
              id="preview-url"
              value={value}
              onChange={(e) => setValue(e.target.value)}
              placeholder="my-site.vercel.app"
              autoFocus
            />
            <div className="flex justify-between gap-2">
              {site.previewUrl ? (
                <Button
                  type="button"
                  variant="ghost"
                  size="sm"
                  onClick={() => save(null)}
                >
                  Remove
                </Button>
              ) : (
                <span />
              )}
              <Button type="submit" size="sm" disabled={patch.isPending}>
                Save
              </Button>
            </div>
          </form>
        </PopoverContent>
      </Popover>
    </>
  )
}

function Tags({ site }: { site: SiteDetail }) {
  const patch = usePatchSite(site.id)
  const [adding, setAdding] = useState(false)
  const [value, setValue] = useState("")
  const names = site.tags.map((tag) => tag.name)

  function save(next: string[]) {
    patch.mutate({ tags: next }, { onError: (e) => toast.error(e.message) })
  }

  return (
    <>
      {site.tags.map((tag) => (
        <span
          key={tag.id}
          className="inline-flex h-6 items-center gap-1 rounded-md border border-border bg-muted/40 pr-1 pl-1.5 text-xs text-muted-foreground"
        >
          {tag.name}
          <button
            type="button"
            aria-label={`Remove tag ${tag.name}`}
            className="rounded hover:bg-muted hover:text-foreground"
            onClick={() => save(names.filter((n) => n !== tag.name))}
          >
            <XIcon className="size-3" />
          </button>
        </span>
      ))}
      {adding ? (
        <form
          onSubmit={(event) => {
            event.preventDefault()
            if (value.trim()) save([...names, value.trim()])
            setValue("")
            setAdding(false)
          }}
        >
          <Input
            autoFocus
            value={value}
            onChange={(e) => setValue(e.target.value)}
            onBlur={() => setAdding(false)}
            placeholder="New tag, Enter"
            className="h-6 w-32 text-xs"
            maxLength={40}
          />
        </form>
      ) : (
        <Button variant="ghost" size="xs" onClick={() => setAdding(true)}>
          <PlusIcon /> Tag
        </Button>
      )}
    </>
  )
}

function FetchBanner({ site }: { site: SiteDetail }) {
  const job = site.activeJob
  if (job) {
    const pct = job.progress.total
      ? ((job.progress.current ?? 0) / job.progress.total) * 100
      : 0
    return (
      <div
        className="mb-4 flex items-center gap-3 rounded-lg border border-sky-500/30 bg-sky-500/5 px-4 py-2.5 text-sm"
        aria-live="polite"
      >
        <LoaderCircleIcon className="size-4 animate-spin text-sky-500" />
        <span>
          {job.status === "queued"
            ? "Waiting for a worker…"
            : "Fetching profile…"}
          {job.progress.label && (
            <span className="ml-2 font-mono text-xs text-muted-foreground">
              {job.progress.label}
            </span>
          )}
        </span>
        <Progress value={pct} className="max-w-xs flex-1" />
      </div>
    )
  }
  return null
}

export function SiteLayout() {
  const { siteId } = routeApi.useParams()
  const wide = useRouterState({
    select: (state) => state.location.pathname.endsWith("/content"),
  })
  const { data: site, error, isLoading } = useSite(siteId)
  const patch = usePatchSite(siteId)
  const fetchProfile = useFetchSite(siteId)
  const archive = useArchiveSite()
  const restore = useRestoreSite()
  const categories = useCategories().data ?? []
  const [editOpen, setEditOpen] = useState(false)
  const [deleteOpen, setDeleteOpen] = useState(false)

  if (isLoading) {
    return <div className="h-40 animate-pulse rounded-xl bg-muted/40" />
  }
  if (error || !site) {
    return (
      <EmptyState
        icon={<GlobeIcon />}
        title={
          error && (error as { status?: number }).status !== 404
            ? "Could not load this site"
            : "Site not found"
        }
        description={error?.message ?? "It may have been deleted."}
        action={
          <Link to="/sites" className={buttonVariants({ variant: "outline" })}>
            Back to sites
          </Link>
        }
      />
    )
  }

  const archived = site.archivedAt !== null

  return (
    <div>
      <div className="flex flex-col gap-4 pb-4">
        {archived && (
          <div className="flex items-center justify-between rounded-lg border border-amber-500/30 bg-amber-500/5 px-4 py-2 text-sm">
            This site is archived.
            <Button
              size="sm"
              variant="outline"
              onClick={() =>
                restore.mutate(site.id, {
                  onSuccess: () => toast.success("Restored"),
                })
              }
            >
              <ArchiveRestoreIcon /> Restore
            </Button>
          </div>
        )}
        <div className="flex flex-wrap items-start justify-between gap-3">
          <div className="flex min-w-0 items-center gap-3">
            <SiteIcon site={site} className="size-10 text-base" />
            <div className="min-w-0">
              <div className="flex items-center gap-1">
                <h1 className="truncate text-xl font-semibold tracking-tight">
                  {site.siteKey}
                </h1>
                <CopyButton value={site.siteKey} />
                <a
                  href={site.url}
                  target="_blank"
                  rel="noopener noreferrer"
                  className={buttonVariants({
                    variant: "ghost",
                    size: "icon-xs",
                  })}
                  aria-label="Open live site"
                >
                  <ExternalLinkIcon />
                </a>
                <PreviewLink site={site} />
              </div>
              <p className="truncate text-sm text-muted-foreground">
                {site.name ?? "No name"}
              </p>
            </div>
          </div>
          <div className="flex flex-wrap items-center gap-2">
            <Button
              variant="outline"
              disabled={!!site.activeJob || fetchProfile.isPending}
              onClick={() =>
                fetchProfile.mutate(undefined, {
                  onError: (e) => toast.error(e.message),
                })
              }
            >
              <RefreshCwIcon
                className={site.activeJob ? "animate-spin" : undefined}
              />{" "}
              Re-fetch profile
            </Button>
            <Link
              to="/sites/$siteId/content"
              params={{ siteId: site.id }}
              className={buttonVariants({ variant: "outline" })}
            >
              <DownloadIcon /> Download content
            </Link>
            <DropdownMenu>
              <DropdownMenuTrigger
                className={buttonVariants({ variant: "ghost", size: "icon" })}
                aria-label="More actions"
              >
                <EllipsisIcon />
              </DropdownMenuTrigger>
              <DropdownMenuContent align="end" className="w-48">
                <DropdownMenuItem onClick={() => setEditOpen(true)}>
                  <PencilIcon /> Edit details…
                </DropdownMenuItem>
                {!archived && (
                  <DropdownMenuItem
                    onClick={() =>
                      archive.mutate(site.id, {
                        onSuccess: () =>
                          toast.success("Archived. Use Restore to undo."),
                      })
                    }
                  >
                    <ArchiveIcon /> Archive
                  </DropdownMenuItem>
                )}
                <DropdownMenuSeparator />
                <DropdownMenuItem
                  variant="destructive"
                  onClick={() => setDeleteOpen(true)}
                >
                  <TrashIcon /> Delete…
                </DropdownMenuItem>
              </DropdownMenuContent>
            </DropdownMenu>
          </div>
        </div>

        <div className="flex flex-wrap items-center gap-2">
          <SimpleSelect
            size="sm"
            value={site.status}
            onChange={(value) =>
              patch.mutate(
                { status: value as SiteStatus },
                { onError: (e) => toast.error(e.message) }
              )
            }
            options={STATUSES.map((value) => ({
              value,
              label: <StatusBadge status={value} />,
            }))}
          />
          <DropdownMenu>
            <DropdownMenuTrigger
              className={buttonVariants({ variant: "outline", size: "sm" })}
            >
              {site.category?.name ?? "No category"}{" "}
              <ChevronDownIcon className="text-muted-foreground" />
            </DropdownMenuTrigger>
            <DropdownMenuContent className="w-48">
              <DropdownMenuItem
                onClick={() => patch.mutate({ categoryId: null })}
              >
                No category
              </DropdownMenuItem>
              {categories.map((item) => (
                <DropdownMenuItem
                  key={item.id}
                  onClick={() => patch.mutate({ categoryId: item.id })}
                >
                  {item.name}
                </DropdownMenuItem>
              ))}
            </DropdownMenuContent>
          </DropdownMenu>
          <Tags site={site} />
        </div>
      </div>

      <FetchBanner site={site} />

      <nav className="mb-5 flex gap-1 overflow-x-auto border-b border-border">
        {tabs.map((tab) => {
          const count = tabCount(site, tab.label)
          return (
            <Link
              key={tab.to}
              to={tab.to}
              params={{ siteId: site.id }}
              activeOptions={{ exact: tab.exact }}
              className="-mb-px flex items-center gap-1.5 border-b-2 border-transparent px-3 py-2 text-sm whitespace-nowrap text-muted-foreground hover:text-foreground"
              activeProps={{
                className: "border-primary! text-foreground font-medium",
              }}
            >
              {tab.label}
              {count ? (
                <span className="text-xs text-muted-foreground">{count}</span>
              ) : null}
            </Link>
          )
        })}
      </nav>

      {/* The Content tab needs the full width for its page list and Markdown view. */}
      <div
        className={cn(
          "grid gap-6",
          !wide && "xl:grid-cols-[minmax(0,1fr)_280px]"
        )}
      >
        <div className="min-w-0">
          <Outlet />
        </div>
        {!wide && <Sidebar site={site} />}
      </div>

      {editOpen && (
        <EditDialog site={site} open={editOpen} onOpenChange={setEditOpen} />
      )}
      {deleteOpen && (
        <DeleteDialog
          site={site}
          open={deleteOpen}
          onOpenChange={setDeleteOpen}
        />
      )}
    </div>
  )
}
