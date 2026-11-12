import { getRouteApi, Link, useNavigate } from "@tanstack/react-router"
import {
  ArchiveIcon,
  ArchiveRestoreIcon,
  BookmarkIcon,
  ChevronDownIcon,
  GlobeIcon,
  LayoutGridIcon,
  RefreshCwIcon,
  Rows3Icon,
  SearchIcon,
  TagIcon,
  XIcon,
} from "lucide-react"
import { useState } from "react"
import { toast } from "sonner"
import { cn } from "cn"

import { OutcomeBadge, StatusBadge, TagChip } from "@/components/common/badges"
import { EmptyState, PageHeader } from "@/components/common/misc"
import { SiteIcon } from "@/components/common/site-icon"
import { SimpleSelect } from "@/components/common/simple-select"
import { Button, buttonVariants } from "@/components/ui/button"
import { Checkbox } from "@/components/ui/checkbox"
import {
  DropdownMenu,
  DropdownMenuCheckboxItem,
  DropdownMenuContent,
  DropdownMenuItem,
  DropdownMenuTrigger,
} from "@/components/ui/dropdown-menu"
import { Input } from "@/components/ui/input"
import {
  Table,
  TableBody,
  TableCell,
  TableHead,
  TableHeader,
  TableRow,
} from "@/components/ui/table"
import { daysUntil, dueLabel, relativeTime } from "@/lib/format"
import {
  joinList,
  sortOptions,
  splitList,
  type SitesSearch,
} from "@/lib/site-filters"
import { STATUSES } from "@/lib/domain"
import {
  useBulkSites,
  useCategories,
  useSites,
  useTags,
  useTaxonomyMutations,
} from "@/api/hooks/sites"
import type { SiteListItem as Site } from "@/api/types"
import {
  Dialog,
  DialogContent,
  DialogFooter,
  DialogHeader,
  DialogTitle,
} from "@/components/ui/dialog"

const routeApi = getRouteApi("/sites")

type FilterKey = "status" | "category" | "tag" | "cms" | "outcome"

function MultiFilter({
  label,
  options,
  selected,
  counts,
  onChange,
}: {
  label: string
  options: string[]
  selected: string[]
  counts: Record<string, number>
  onChange: (values: string[]) => void
}) {
  return (
    <DropdownMenu>
      <DropdownMenuTrigger
        className={cn(
          buttonVariants({ variant: "outline", size: "sm" }),
          selected.length && "border-primary/50"
        )}
      >
        {label}
        {selected.length > 0 && (
          <span className="rounded bg-primary/20 px-1 text-xs">
            {selected.length}
          </span>
        )}
        <ChevronDownIcon className="text-muted-foreground" />
      </DropdownMenuTrigger>
      <DropdownMenuContent className="w-56">
        {options.map((option) => (
          <DropdownMenuCheckboxItem
            key={option}
            checked={selected.includes(option)}
            onCheckedChange={(checked) =>
              onChange(
                checked
                  ? [...selected, option]
                  : selected.filter((item) => item !== option)
              )
            }
          >
            <span className="flex-1 truncate">{option}</span>
            <span className="text-xs text-muted-foreground">
              {counts[option] ?? 0}
            </span>
          </DropdownMenuCheckboxItem>
        ))}
      </DropdownMenuContent>
    </DropdownMenu>
  )
}

function SiteIdentity({ site }: { site: Site }) {
  return (
    <Link
      to="/sites/$siteId"
      params={{ siteId: site.id }}
      className="flex min-w-0 items-center gap-2.5"
    >
      <SiteIcon site={site} className="size-7 text-xs" />
      <span className="min-w-0">
        <span className="block truncate font-medium hover:underline">
          {site.siteKey}
        </span>
        {site.name && (
          <span className="block truncate text-xs text-muted-foreground">
            {site.name}
          </span>
        )}
      </span>
    </Link>
  )
}

function CmsCell({ site }: { site: Site }) {
  if (!site.cms) return <span className="text-muted-foreground">Unknown</span>
  return (
    <span>
      {site.cms}
      {site.cmsVersion && (
        <span className="ml-1 text-xs text-muted-foreground">
          {site.cmsVersion}
        </span>
      )}
    </span>
  )
}

function SslCell({ site }: { site: Site }) {
  const days = daysUntil(site.tlsValidUntil)
  if (days === null) return <span className="text-muted-foreground">—</span>
  const tone =
    days < 14
      ? "text-rose-600 dark:text-rose-300"
      : days < 30
        ? "text-amber-600 dark:text-amber-300"
        : ""
  return <span className={tone}>{days}d</span>
}

export function SitesPage() {
  const search = routeApi.useSearch()
  const navigate = useNavigate({ from: "/sites" })
  const [selected, setSelected] = useState<number[]>([])
  const [saveOpen, setSaveOpen] = useState(false)
  const [tagOpen, setTagOpen] = useState(false)

  const { data, error, isLoading, isFetching } = useSites({
    ...search,
    perPage: 50,
  })
  const categories = useCategories().data ?? []
  const allTags = useTags().data ?? []
  const bulkMutation = useBulkSites()
  const sites = data?.items ?? []
  const total = data?.total ?? 0
  const view = search.view ?? "table"
  const archivedView = search.archived === "1"
  const page = data?.page ?? 1
  const pages = Math.max(1, Math.ceil(total / (data?.perPage ?? 50)))

  const emptyFacets = {
    status: {},
    category: {},
    tag: {},
    cms: {},
    outcome: {},
  }
  const facets = data?.facets ?? emptyFacets
  const cmsOptions = [
    ...new Set([...Object.keys(facets.cms), ...splitList(search.cms)]),
  ].sort()

  function setSearch(patch: Partial<SitesSearch>) {
    // Any filter change goes back to the first page; paging itself passes `page` explicitly.
    navigate({
      search: (prev) => ({ ...prev, page: undefined, ...patch }),
      replace: true,
    })
  }

  function setFilter(key: FilterKey, values: string[]) {
    setSearch({ [key]: joinList(values) })
  }

  const activeFilters = (
    ["status", "category", "tag", "cms", "outcome"] as const
  ).flatMap((key) => splitList(search[key]).map((value) => ({ key, value })))

  const allSelected =
    sites.length > 0 && sites.every((site) => selected.includes(site.id))

  function toggle(id: number, checked: boolean) {
    setSelected((prev) =>
      checked ? [...prev, id] : prev.filter((item) => item !== id)
    )
  }

  function bulk(
    action: Parameters<typeof bulkMutation.mutate>[0]["action"],
    value?: unknown,
    done?: string
  ) {
    bulkMutation.mutate(
      { ids: selected, action, value },
      {
        onSuccess: (result) => {
          toast.success(
            done ??
              `${result.updated} site${result.updated === 1 ? "" : "s"} updated`
          )
          if (
            action !== "setStatus" &&
            action !== "addTag" &&
            action !== "removeTag" &&
            action !== "setCategory"
          )
            setSelected([])
        },
        onError: (e) => toast.error(e.message),
      }
    )
  }

  return (
    <div>
      <PageHeader
        title={archivedView ? "Archived sites" : "Sites"}
        description={
          isLoading
            ? "Loading…"
            : `${total} site${total === 1 ? "" : "s"}${isFetching ? " · updating…" : ""}`
        }
        actions={
          <>
            <Button
              variant={archivedView ? "secondary" : "outline"}
              onClick={() => {
                setSelected([])
                setSearch({ archived: archivedView ? undefined : "1" })
              }}
            >
              <ArchiveIcon />
              {archivedView ? "Back to sites" : "Archived"}
            </Button>
            <div className="flex rounded-lg border border-border p-0.5">
              <Button
                size="icon-sm"
                variant={view === "table" ? "secondary" : "ghost"}
                onClick={() => setSearch({ view: undefined })}
                aria-label="Table view"
              >
                <Rows3Icon />
              </Button>
              <Button
                size="icon-sm"
                variant={view === "cards" ? "secondary" : "ghost"}
                onClick={() => setSearch({ view: "cards" })}
                aria-label="Card view"
              >
                <LayoutGridIcon />
              </Button>
            </div>
            <Button variant="outline" onClick={() => setSaveOpen(true)}>
              <BookmarkIcon />
              Save view
            </Button>
          </>
        }
      />

      <div className="flex flex-wrap items-center gap-2 pb-3">
        <div className="relative w-full sm:w-64">
          <SearchIcon className="pointer-events-none absolute top-1/2 left-2.5 size-4 -translate-y-1/2 text-muted-foreground" />
          <Input
            value={search.q ?? ""}
            onChange={(event) =>
              setSearch({ q: event.target.value || undefined })
            }
            placeholder="Domain or name…"
            className="pl-8"
          />
        </div>
        <MultiFilter
          label="Status"
          options={[...STATUSES]}
          selected={splitList(search.status)}
          counts={facets.status}
          onChange={(values) => setFilter("status", values)}
        />
        <MultiFilter
          label="Category"
          options={categories.map((category) => category.name)}
          selected={splitList(search.category)}
          counts={facets.category}
          onChange={(values) => setFilter("category", values)}
        />
        <MultiFilter
          label="Tags"
          options={allTags.map((tag) => tag.name)}
          selected={splitList(search.tag)}
          counts={facets.tag}
          onChange={(values) => setFilter("tag", values)}
        />
        <MultiFilter
          label="CMS"
          options={cmsOptions}
          selected={splitList(search.cms)}
          counts={facets.cms}
          onChange={(values) => setFilter("cms", values)}
        />
        <MultiFilter
          label="Last fetch"
          options={["ok", "partial", "failed", "never"]}
          selected={splitList(search.outcome)}
          counts={facets.outcome}
          onChange={(values) => setFilter("outcome", values)}
        />
        <SimpleSelect
          size="sm"
          className="ml-auto"
          value={search.sort ?? "-updated"}
          onChange={(value) =>
            setSearch({
              sort: value === "-updated" ? undefined : (value ?? undefined),
            })
          }
          options={sortOptions.map((option) => ({ ...option }))}
        />
      </div>

      {activeFilters.length > 0 && (
        <div className="flex flex-wrap items-center gap-1.5 pb-3">
          {activeFilters.map(({ key, value }) => (
            <button
              key={`${key}:${value}`}
              onClick={() =>
                setFilter(
                  key,
                  splitList(search[key]).filter((item) => item !== value)
                )
              }
              className="inline-flex h-6 items-center gap-1 rounded-md border border-border bg-muted/40 px-2 text-xs hover:bg-muted"
            >
              <span className="text-muted-foreground">{key}:</span> {value}
              <XIcon className="size-3" />
            </button>
          ))}
          <Button
            variant="ghost"
            size="xs"
            onClick={() =>
              navigate({ search: { view: search.view }, replace: true })
            }
          >
            Clear all
          </Button>
        </div>
      )}

      {selected.length > 0 && (
        <div className="sticky top-16 z-20 mb-3 flex flex-wrap items-center gap-2 rounded-lg border border-primary/30 bg-card px-3 py-2 shadow-sm">
          <span className="text-sm font-medium">
            {selected.length} selected
          </span>
          <DropdownMenu>
            <DropdownMenuTrigger
              className={buttonVariants({ variant: "outline", size: "sm" })}
            >
              Set status <ChevronDownIcon />
            </DropdownMenuTrigger>
            <DropdownMenuContent className="w-40">
              {STATUSES.map((status) => (
                <DropdownMenuItem
                  key={status}
                  onClick={() => bulk("setStatus", status)}
                >
                  <StatusBadge status={status} />
                </DropdownMenuItem>
              ))}
            </DropdownMenuContent>
          </DropdownMenu>
          {archivedView ? (
            <Button
              variant="outline"
              size="sm"
              onClick={() => bulk("restore", undefined, "Restored")}
            >
              <ArchiveRestoreIcon /> Restore
            </Button>
          ) : (
            <>
              <Button
                variant="outline"
                size="sm"
                onClick={() => setTagOpen(true)}
              >
                <TagIcon /> Add tag
              </Button>
              <Button
                variant="outline"
                size="sm"
                onClick={() =>
                  bulk("refetch", undefined, "Profile fetches queued")
                }
              >
                <RefreshCwIcon /> Re-fetch
              </Button>
              <Button
                variant="outline"
                size="sm"
                onClick={() => bulk("archive", undefined, "Archived")}
              >
                <ArchiveIcon /> Archive
              </Button>
            </>
          )}
          <Button
            variant="ghost"
            size="sm"
            className="ml-auto"
            onClick={() => setSelected([])}
          >
            Clear selection
          </Button>
        </div>
      )}

      {error ? (
        <p className="text-sm text-destructive">{error.message}</p>
      ) : isLoading ? (
        <div className="space-y-2">
          {[0, 1, 2, 3].map((i) => (
            <div
              key={i}
              className="h-12 animate-pulse rounded-lg bg-muted/50"
            />
          ))}
        </div>
      ) : sites.length === 0 ? (
        <EmptyState
          icon={<GlobeIcon />}
          title={
            activeFilters.length
              ? "No sites match these filters"
              : "No sites yet"
          }
          description={
            activeFilters.length
              ? "Change or clear the filters to see more sites."
              : "Use “Add site” to enter a client website. Its profile is fetched in the background."
          }
          action={
            <Button
              variant="outline"
              onClick={() => navigate({ search: {}, replace: true })}
            >
              Clear filters
            </Button>
          }
        />
      ) : view === "cards" ? (
        <div className="grid gap-3 sm:grid-cols-2 xl:grid-cols-3">
          {sites.map((site) => (
            <div
              key={site.id}
              className="rounded-xl border border-border bg-card/60 p-4"
            >
              <div className="flex items-start justify-between gap-2">
                <SiteIdentity site={site} />
                <StatusBadge status={site.status} />
              </div>
              <div className="mt-3 flex items-center justify-between text-sm">
                <CmsCell site={site} />
                <OutcomeBadge outcome={site.lastFetchOutcome} />
              </div>
              <div className="mt-3 flex flex-wrap gap-1">
                {site.tags.map((tag) => (
                  <TagChip key={tag.id} name={tag.name} />
                ))}
              </div>
              <div className="mt-3 flex justify-between border-t border-border pt-2 text-xs text-muted-foreground">
                <span>Fetched {relativeTime(site.lastFetchedAt)}</span>
                <span>{site.openTodos} open to-dos</span>
              </div>
            </div>
          ))}
        </div>
      ) : (
        <div className="rounded-xl border border-border">
          <Table>
            <TableHeader>
              <TableRow>
                <TableHead className="w-8">
                  <Checkbox
                    checked={allSelected}
                    onCheckedChange={(checked) =>
                      setSelected(checked ? sites.map((site) => site.id) : [])
                    }
                    aria-label="Select all"
                  />
                </TableHead>
                <TableHead>Domain</TableHead>
                <TableHead>Backend / CMS</TableHead>
                <TableHead>Status</TableHead>
                <TableHead className="hidden md:table-cell">Category</TableHead>
                <TableHead className="hidden lg:table-cell">Tags</TableHead>
                <TableHead>Fetched</TableHead>
                <TableHead className="hidden md:table-cell">SSL</TableHead>
                <TableHead className="hidden xl:table-cell">Deadline</TableHead>
                <TableHead className="text-right">To-dos</TableHead>
              </TableRow>
            </TableHeader>
            <TableBody>
              {sites.map((site) => {
                const deadlineDays = daysUntil(site.deadline)
                return (
                  <TableRow
                    key={site.id}
                    data-state={
                      selected.includes(site.id) ? "selected" : undefined
                    }
                  >
                    <TableCell>
                      <Checkbox
                        checked={selected.includes(site.id)}
                        onCheckedChange={(checked) => toggle(site.id, checked)}
                        aria-label={`Select ${site.siteKey}`}
                      />
                    </TableCell>
                    <TableCell className="max-w-56">
                      <SiteIdentity site={site} />
                    </TableCell>
                    <TableCell>
                      <CmsCell site={site} />
                    </TableCell>
                    <TableCell>
                      <StatusBadge status={site.status} />
                    </TableCell>
                    <TableCell className="hidden text-muted-foreground md:table-cell">
                      {site.category?.name ?? "—"}
                    </TableCell>
                    <TableCell className="hidden lg:table-cell">
                      <div className="flex gap-1">
                        {site.tags.slice(0, 3).map((tag) => (
                          <TagChip key={tag.id} name={tag.name} />
                        ))}
                      </div>
                    </TableCell>
                    <TableCell>
                      <div className="flex items-center gap-1.5 whitespace-nowrap">
                        {site.lastFetchOutcome === "failed" && (
                          <span
                            className="size-1.5 rounded-full bg-rose-400"
                            title="Last fetch failed"
                          />
                        )}
                        <span className="text-muted-foreground">
                          {relativeTime(site.lastFetchedAt)}
                        </span>
                      </div>
                    </TableCell>
                    <TableCell className="hidden md:table-cell">
                      <SslCell site={site} />
                    </TableCell>
                    <TableCell
                      className={cn(
                        "hidden whitespace-nowrap xl:table-cell",
                        deadlineDays !== null && deadlineDays < 0
                          ? "text-rose-600 dark:text-rose-300"
                          : "text-muted-foreground"
                      )}
                    >
                      {site.deadline ? dueLabel(site.deadline) : "—"}
                    </TableCell>
                    <TableCell className="text-right">
                      {site.openTodos || "—"}
                    </TableCell>
                  </TableRow>
                )
              })}
            </TableBody>
          </Table>
        </div>
      )}
      {pages > 1 && (
        <div className="mt-3 flex items-center justify-between text-sm text-muted-foreground">
          <span>
            Page {page} of {pages}
          </span>
          <div className="flex gap-2">
            <Button
              variant="outline"
              size="sm"
              disabled={page <= 1}
              onClick={() =>
                navigate({
                  search: (prev) => ({ ...prev, page: page - 1 }),
                  replace: true,
                })
              }
            >
              Previous
            </Button>
            <Button
              variant="outline"
              size="sm"
              disabled={page >= pages}
              onClick={() =>
                navigate({
                  search: (prev) => ({ ...prev, page: page + 1 }),
                  replace: true,
                })
              }
            >
              Next
            </Button>
          </div>
        </div>
      )}

      <SaveViewDialog
        open={saveOpen}
        onOpenChange={setSaveOpen}
        search={search}
      />
      <AddTagDialog
        open={tagOpen}
        onOpenChange={setTagOpen}
        existing={allTags.map((tag) => tag.name)}
        onSubmit={(name) => {
          bulk("addTag", name, `Tag “${name}” added`)
          setTagOpen(false)
        }}
      />
    </div>
  )
}

function SaveViewDialog({
  open,
  onOpenChange,
  search,
}: {
  open: boolean
  onOpenChange: (open: boolean) => void
  search: SitesSearch
}) {
  const [name, setName] = useState("")
  const { create } = useTaxonomyMutations("views")
  const params = new URLSearchParams()
  for (const [key, value] of Object.entries(search))
    if (value !== undefined && key !== "page") params.set(key, String(value))
  const query = params.toString()

  return (
    <Dialog open={open} onOpenChange={onOpenChange}>
      <DialogContent>
        <DialogHeader>
          <DialogTitle>Save this view</DialogTitle>
        </DialogHeader>
        <form
          className="space-y-3"
          onSubmit={(event) => {
            event.preventDefault()
            create.mutate(
              { name, query },
              {
                onSuccess: () => {
                  toast.success(`View “${name}” saved`)
                  setName("")
                  onOpenChange(false)
                },
                onError: (e) => toast.error(e.message),
              }
            )
          }}
        >
          <Input
            value={name}
            onChange={(e) => setName(e.target.value)}
            placeholder="Name, e.g. Waiting on client"
            autoFocus
            maxLength={60}
          />
          <p className="font-mono text-xs break-all text-muted-foreground">
            {query || "No filters: all sites"}
          </p>
          <DialogFooter>
            <Button type="submit" disabled={!name.trim() || create.isPending}>
              Save view
            </Button>
          </DialogFooter>
        </form>
      </DialogContent>
    </Dialog>
  )
}

function AddTagDialog({
  open,
  onOpenChange,
  existing,
  onSubmit,
}: {
  open: boolean
  onOpenChange: (open: boolean) => void
  existing: string[]
  onSubmit: (name: string) => void
}) {
  const [name, setName] = useState("")
  return (
    <Dialog open={open} onOpenChange={onOpenChange}>
      <DialogContent>
        <DialogHeader>
          <DialogTitle>Add a tag to the selected sites</DialogTitle>
        </DialogHeader>
        <form
          className="space-y-3"
          onSubmit={(event) => {
            event.preventDefault()
            if (name.trim()) {
              onSubmit(name.trim())
              setName("")
            }
          }}
        >
          <Input
            value={name}
            onChange={(e) => setName(e.target.value)}
            placeholder="Tag name"
            list="tag-options"
            autoFocus
            maxLength={40}
          />
          <datalist id="tag-options">
            {existing.map((tag) => (
              <option key={tag} value={tag} />
            ))}
          </datalist>
          <DialogFooter>
            <Button type="submit" disabled={!name.trim()}>
              Add tag
            </Button>
          </DialogFooter>
        </form>
      </DialogContent>
    </Dialog>
  )
}
