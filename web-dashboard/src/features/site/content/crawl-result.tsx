import {
  ChevronRightIcon,
  DownloadIcon,
  ExternalLinkIcon,
  FileIcon,
  FolderOpenIcon,
  SearchIcon,
  TrashIcon,
  XIcon,
} from "lucide-react"
import { useEffect, useMemo, useState } from "react"
import { toast } from "sonner"
import { cn } from "cn"

import {
  crawlArchiveUrl,
  crawlFileUrl,
  useBuildArchive,
  useCrawl,
  useCrawlAssets,
  useCrawlPage,
  useCrawlPages,
  useDeleteCrawl,
  useRevealCrawl,
} from "@/api/hooks/crawls"
import type {
  CrawlAsset,
  CrawlDetail,
  CrawlPageItem,
  CrawlTreeNode,
} from "@/api/types"
import { CopyButton, EmptyState } from "@/components/common/misc"
import { Markdown } from "@/components/common/markdown"
import { Button } from "@/components/ui/button"
import {
  Dialog,
  DialogContent,
  DialogDescription,
  DialogFooter,
  DialogHeader,
  DialogTitle,
} from "@/components/ui/dialog"
import { Input } from "@/components/ui/input"
import { Tabs, TabsContent, TabsList, TabsTrigger } from "@/components/ui/tabs"
import { withLocalImages } from "@/lib/crawl-paths"
import { formatBytes } from "@/lib/format"

function TreeNode({
  node,
  depth,
  selected,
  onSelect,
}: {
  node: CrawlTreeNode
  depth: number
  selected: string | null
  onSelect: (id: string) => void
}) {
  const [open, setOpen] = useState(depth < 1)
  const has = node.children.length > 0
  return (
    <li>
      <div
        className={cn(
          "flex items-center gap-1 rounded px-1 py-0.5 text-sm",
          node.page && selected === node.page && "bg-accent"
        )}
        style={{ paddingLeft: depth * 12 }}
      >
        {has ? (
          <button
            type="button"
            onClick={() => setOpen(!open)}
            aria-label="Toggle"
          >
            <ChevronRightIcon
              className={cn(
                "size-3.5 transition-transform",
                open && "rotate-90"
              )}
            />
          </button>
        ) : (
          <span className="w-3.5" />
        )}
        {node.page ? (
          <button
            type="button"
            className="min-w-0 truncate text-left hover:underline"
            onClick={() => onSelect(node.page!)}
          >
            {node.name === "/" ? "(home)" : node.name}
          </button>
        ) : (
          <span className="truncate text-muted-foreground">{node.name}</span>
        )}
      </div>
      {has && open && (
        <ul>
          {node.children.map((child) => (
            <TreeNode
              key={child.name}
              node={child}
              depth={depth + 1}
              selected={selected}
              onSelect={onSelect}
            />
          ))}
        </ul>
      )}
    </li>
  )
}

function PagesPane({ crawl }: { crawl: CrawlDetail }) {
  const { data, isLoading } = useCrawlPages(crawl.id, true)
  const [q, setQ] = useState("")
  const [pageId, setPageId] = useState<string | null>(null)
  const page = useCrawlPage(crawl.id, pageId)
  const items = useMemo(() => {
    const needle = q.trim().toLowerCase()
    return (data?.items ?? []).filter(
      (p) =>
        !needle ||
        p.title.toLowerCase().includes(needle) ||
        p.url.toLowerCase().includes(needle)
    )
  }, [data, q])

  if (isLoading)
    return <div className="h-40 animate-pulse rounded-xl bg-muted/40" />
  if (!data?.items.length)
    return (
      <EmptyState
        icon={<FileIcon />}
        title="No pages saved"
        description="This crawl did not export any page."
      />
    )
  const current = page.data
  return (
    <div className="grid gap-4 lg:grid-cols-[16rem_minmax(0,1fr)]">
      <div className="grid content-start gap-2">
        <div className="relative">
          <SearchIcon className="absolute top-2 left-2 size-4 text-muted-foreground" />
          <Input
            value={q}
            onChange={(e) => setQ(e.target.value)}
            placeholder="Search pages"
            className="pl-8"
          />
        </div>
        <div className="max-h-[32rem] overflow-auto rounded-lg border p-1">
          {q.trim() ? (
            <ul>
              {items.map((p: CrawlPageItem) => (
                <li key={p.id}>
                  <button
                    type="button"
                    onClick={() => setPageId(p.id)}
                    className={cn(
                      "w-full rounded px-2 py-1 text-left text-sm hover:bg-accent",
                      pageId === p.id && "bg-accent"
                    )}
                  >
                    <div className="truncate">{p.title || p.url}</div>
                    <div className="truncate text-xs text-muted-foreground">
                      {p.file}
                    </div>
                  </button>
                </li>
              ))}
              {items.length === 0 && (
                <li className="p-2 text-sm text-muted-foreground">No match.</li>
              )}
            </ul>
          ) : data.tree ? (
            <ul>
              <TreeNode
                node={data.tree}
                depth={0}
                selected={pageId}
                onSelect={setPageId}
              />
            </ul>
          ) : null}
        </div>
      </div>
      <div className="min-w-0">
        {!pageId ? (
          <p className="text-sm text-muted-foreground">
            Pick a page to read its Markdown.
          </p>
        ) : page.isLoading ? (
          <div className="h-40 animate-pulse rounded-xl bg-muted/40" />
        ) : current ? (
          <div className="grid gap-3">
            <div className="flex flex-wrap items-center gap-2">
              <h3 className="mr-auto min-w-0 truncate text-lg font-semibold">
                {current.meta.title || current.meta.url}
              </h3>
              <CopyButton value={current.markdown} />
              <Button
                variant="outline"
                size="sm"
                render={
                  <a
                    href={current.meta.url}
                    target="_blank"
                    rel="noopener noreferrer"
                  />
                }
              >
                <ExternalLinkIcon /> Original
              </Button>
            </div>
            <div className="flex flex-wrap gap-x-4 gap-y-1 text-xs text-muted-foreground">
              <span className="font-mono">{current.meta.file}</span>
              <span>{current.meta.word_count} words</span>
              <span>{current.meta.images.length} images</span>
              <span>{current.meta.headings.length} headings</span>
            </div>
            <div className="overflow-x-auto rounded-lg border p-4 break-words">
              <Markdown>
                {withLocalImages(current.markdown, current.meta.file, (path) =>
                  crawlFileUrl(crawl.id, path)
                )}
              </Markdown>
            </div>
          </div>
        ) : (
          <p className="text-sm text-destructive">Could not load the page.</p>
        )}
      </div>
    </div>
  )
}

function Lightbox({
  crawlId,
  asset,
  pages,
  onClose,
}: {
  crawlId: number
  asset: CrawlAsset
  pages: Map<string, CrawlPageItem>
  onClose: () => void
}) {
  return (
    <Dialog open onOpenChange={(open) => !open && onClose()}>
      <DialogContent className="sm:max-w-3xl">
        <DialogHeader>
          <DialogTitle className="truncate font-mono text-sm">
            {asset.file.split("/").pop()}
          </DialogTitle>
          <DialogDescription>
            {asset.width && asset.height
              ? `${asset.width} × ${asset.height} · `
              : ""}
            {formatBytes(asset.bytes)} · {asset.mime}
          </DialogDescription>
        </DialogHeader>
        <div className="flex max-h-[60vh] items-center justify-center overflow-auto rounded-lg bg-muted/30 p-2">
          <img
            src={crawlFileUrl(crawlId, asset.file)}
            alt={asset.altTexts[0] ?? ""}
            className="max-h-[56vh] max-w-full object-contain"
          />
        </div>
        {asset.altTexts.length > 0 && (
          <p className="text-sm">
            <span className="text-muted-foreground">Alt: </span>
            {asset.altTexts.join(" · ")}
          </p>
        )}
        {asset.usedOn.length > 0 && (
          <p className="text-xs text-muted-foreground">
            Used on:{" "}
            {asset.usedOn
              .slice(0, 8)
              .map((id) => pages.get(id)?.title || pages.get(id)?.url || id)
              .join(", ")}
            {asset.usedOn.length > 8 && ` and ${asset.usedOn.length - 8} more`}
          </p>
        )}
        <DialogFooter>
          <Button
            variant="outline"
            render={
              <a href={crawlFileUrl(crawlId, asset.file, true)} download />
            }
          >
            <DownloadIcon /> Download
          </Button>
        </DialogFooter>
      </DialogContent>
    </Dialog>
  )
}

function ImagesPane({ crawl }: { crawl: CrawlDetail }) {
  const { data, isLoading } = useCrawlAssets(crawl.id, "image", true)
  const pages = useCrawlPages(crawl.id, true).data?.items
  const byId = useMemo(
    () => new Map((pages ?? []).map((p) => [p.id, p])),
    [pages]
  )
  const [minWidth, setMinWidth] = useState("")
  const [open, setOpen] = useState<CrawlAsset | null>(null)
  const items = (data?.items ?? []).filter(
    (a) => !minWidth || (a.width ?? 0) >= Number(minWidth)
  )
  if (isLoading)
    return <div className="h-40 animate-pulse rounded-xl bg-muted/40" />
  if (!data?.items.length)
    return <EmptyState icon={<FileIcon />} title="No images downloaded" />
  return (
    <div className="grid gap-3">
      <div className="flex items-center gap-2 text-sm">
        <span className="text-muted-foreground">
          {items.length} of {data.total}
          {data.total > data.items.length && " (first 200 loaded)"}
        </span>
        <Input
          type="number"
          min={0}
          placeholder="min width px"
          value={minWidth}
          onChange={(e) => setMinWidth(e.target.value)}
          className="ml-auto w-36"
        />
      </div>
      <div className="grid grid-cols-2 gap-2 sm:grid-cols-4 lg:grid-cols-6">
        {items.map((a) => (
          <button
            key={a.id}
            type="button"
            onClick={() => setOpen(a)}
            className="group overflow-hidden rounded-lg border bg-muted/30 text-left"
          >
            <div className="aspect-square">
              <img
                src={crawlFileUrl(crawl.id, a.file)}
                alt={a.altTexts[0] ?? ""}
                loading="lazy"
                className="size-full object-contain"
              />
            </div>
            <div className="truncate border-t px-2 py-1 text-xs text-muted-foreground">
              {a.width ? `${a.width}×${a.height} · ` : ""}
              {formatBytes(a.bytes)}
            </div>
          </button>
        ))}
      </div>
      {open && (
        <Lightbox
          crawlId={crawl.id}
          asset={open}
          pages={byId}
          onClose={() => setOpen(null)}
        />
      )}
    </div>
  )
}

function FilesPane({ crawl }: { crawl: CrawlDetail }) {
  const { data, isLoading } = useCrawlAssets(crawl.id, "file", true)
  const pages = useCrawlPages(crawl.id, true).data?.items
  const byId = useMemo(
    () => new Map((pages ?? []).map((p) => [p.id, p])),
    [pages]
  )
  if (isLoading)
    return <div className="h-40 animate-pulse rounded-xl bg-muted/40" />
  if (!data?.items.length)
    return <EmptyState icon={<FileIcon />} title="No files downloaded" />
  return (
    <div className="overflow-auto rounded-lg border">
      <table className="w-full text-sm">
        <thead className="bg-muted/40 text-left text-xs text-muted-foreground">
          <tr>
            <th className="px-3 py-2">Name</th>
            <th className="px-3 py-2">Type</th>
            <th className="px-3 py-2">Size</th>
            <th className="px-3 py-2">Linked from</th>
            <th />
          </tr>
        </thead>
        <tbody>
          {data.items.map((a) => (
            <tr key={a.id} className="border-t">
              <td className="px-3 py-2 font-mono text-xs">
                {a.file.split("/").pop()}
              </td>
              <td className="px-3 py-2">{a.mime}</td>
              <td className="px-3 py-2 tabular-nums">{formatBytes(a.bytes)}</td>
              <td className="max-w-64 truncate px-3 py-2 text-muted-foreground">
                {a.usedOn
                  .map((id) => byId.get(id)?.title || byId.get(id)?.url || id)
                  .join(", ")}
              </td>
              <td className="px-3 py-2 text-right">
                <Button
                  variant="ghost"
                  size="icon-sm"
                  aria-label="Download"
                  render={
                    <a href={crawlFileUrl(crawl.id, a.file, true)} download />
                  }
                >
                  <DownloadIcon />
                </Button>
              </td>
            </tr>
          ))}
        </tbody>
      </table>
    </div>
  )
}

function StructurePane({ crawl }: { crawl: CrawlDetail }) {
  const { data } = useCrawlPages(crawl.id, true)
  const orphans = (data?.items ?? []).filter((p) => p.depth > 0).length
  const errors = crawl.manifest?.errors ?? []
  return (
    <div className="grid gap-4">
      <div className="grid grid-cols-2 gap-2 sm:grid-cols-4">
        {[
          ["Pages", data?.items.length ?? 0],
          ["Deeper than home", orphans],
          [
            "Longest page",
            Math.max(0, ...(data?.items ?? []).map((p) => p.wordCount)) +
              " words",
          ],
          ["Errors", errors.length],
        ].map(([label, value]) => (
          <div key={label} className="rounded-lg border bg-card px-3 py-2">
            <div className="text-xs text-muted-foreground">{label}</div>
            <div className="text-lg font-semibold tabular-nums">{value}</div>
          </div>
        ))}
      </div>
      <p className="text-sm text-muted-foreground">
        The full index for agents is in <code>structure.json</code>,{" "}
        <code>assets.json</code>, <code>urls.json</code> and{" "}
        <code>README.md</code>.
      </p>
      <div className="flex flex-wrap gap-2">
        {[
          "README.md",
          "structure.json",
          "assets.json",
          "urls.json",
          "manifest.json",
          "logs/crawl.log",
        ].map((file) => (
          <Button
            key={file}
            variant="outline"
            size="sm"
            render={
              <a
                href={crawlFileUrl(crawl.id, file)}
                target="_blank"
                rel="noopener noreferrer"
              />
            }
          >
            {file}
          </Button>
        ))}
      </div>
      {errors.length > 0 && (
        <div className="grid gap-1">
          <h4 className="text-sm font-medium">Errors</h4>
          <ul className="max-h-60 overflow-auto rounded-lg border p-2 font-mono text-xs">
            {errors.slice(0, 100).map((e, i) => (
              <li key={i} className="py-0.5">
                <span className="text-muted-foreground">{e.url}</span>{" "}
                {e.message}
              </li>
            ))}
          </ul>
        </div>
      )}
    </div>
  )
}

function ArchiveButton({ crawl }: { crawl: CrawlDetail }) {
  const build = useBuildArchive()
  const [waiting, setWaiting] = useState(false)
  const { refetch } = useCrawl(crawl.id)

  useEffect(() => {
    if (!waiting) return
    if (crawl.archiveReady) {
      // eslint-disable-next-line react-hooks/set-state-in-effect -- one-shot: stop waiting and start the download
      setWaiting(false)
      window.location.assign(crawlArchiveUrl(crawl.id))
      return
    }
    const timer = setInterval(() => refetch(), 1000)
    return () => clearInterval(timer)
  }, [waiting, crawl.archiveReady, crawl.id, refetch])

  async function click() {
    if (crawl.archiveReady) {
      window.location.assign(crawlArchiveUrl(crawl.id))
      return
    }
    try {
      const result = await build.mutateAsync(crawl.id)
      if (result.ready) window.location.assign(crawlArchiveUrl(crawl.id))
      else setWaiting(true)
    } catch (e) {
      toast.error(e instanceof Error ? e.message : "Could not build the ZIP")
    }
  }
  return (
    <Button
      variant="outline"
      size="sm"
      disabled={build.isPending || waiting}
      onClick={click}
    >
      <DownloadIcon /> {waiting ? "Building ZIP…" : "Download ZIP"}
    </Button>
  )
}

/** Header actions plus the browser for one finished crawl. */
export function CrawlResult({
  crawl,
  onDeleted,
}: {
  crawl: CrawlDetail
  onDeleted: () => void
}) {
  const remove = useDeleteCrawl(crawl.siteId)
  const reveal = useRevealCrawl()
  const [confirm, setConfirm] = useState(false)
  const stop = crawl.stopReason
  const stopText: Record<string, string> = {
    max_pages: "Stopped at the page limit.",
    size_cap: "Stopped at the total size cap.",
    blocked: "The site blocked the crawler.",
    canceled: "Canceled.",
  }

  return (
    <div className="grid gap-4">
      <div className="flex flex-wrap items-center gap-2">
        <ArchiveButton crawl={crawl} />
        <Button
          variant="outline"
          size="sm"
          onClick={() =>
            reveal.mutate(crawl.id, {
              onError: (e) =>
                toast.error(
                  `${e.message} Path copied is available next to it.`
                ),
            })
          }
        >
          <FolderOpenIcon /> Open folder
        </Button>
        <span className="inline-flex items-center gap-1 rounded-md border px-2 py-1 font-mono text-xs">
          <span className="max-w-72 truncate">{crawl.path}</span>
          <CopyButton value={crawl.path} />
        </span>
        <Button
          variant="ghost"
          size="sm"
          className="ml-auto text-destructive"
          onClick={() => setConfirm(true)}
        >
          <TrashIcon /> Delete
        </Button>
      </div>
      {stop && stopText[stop] && (
        <p className="text-sm text-amber-600 dark:text-amber-300">
          {stopText[stop]}
        </p>
      )}
      {crawl.pages === 0 && crawl.status === "failed" && (
        <p className="flex items-center gap-1 text-sm text-destructive">
          <XIcon className="size-4" />
          {crawl.manifest?.errors?.[0]?.message ??
            "The crawl produced no pages."}
        </p>
      )}
      <Tabs defaultValue="pages">
        <TabsList>
          <TabsTrigger value="pages">Pages ({crawl.pages})</TabsTrigger>
          <TabsTrigger value="images">Images ({crawl.images})</TabsTrigger>
          <TabsTrigger value="files">Files ({crawl.files})</TabsTrigger>
          <TabsTrigger value="structure">Structure</TabsTrigger>
        </TabsList>
        <TabsContent value="pages" className="pt-3">
          <PagesPane crawl={crawl} />
        </TabsContent>
        <TabsContent value="images" className="pt-3">
          <ImagesPane crawl={crawl} />
        </TabsContent>
        <TabsContent value="files" className="pt-3">
          <FilesPane crawl={crawl} />
        </TabsContent>
        <TabsContent value="structure" className="pt-3">
          <StructurePane crawl={crawl} />
        </TabsContent>
      </Tabs>
      <Dialog open={confirm} onOpenChange={setConfirm}>
        <DialogContent className="sm:max-w-sm">
          <DialogHeader>
            <DialogTitle>Delete this crawl?</DialogTitle>
            <DialogDescription>
              The folder with {crawl.pages} pages, {crawl.images} images and{" "}
              {crawl.files} files ({formatBytes(crawl.bytes)}) is removed from
              disk.
            </DialogDescription>
          </DialogHeader>
          <DialogFooter>
            <Button variant="outline" onClick={() => setConfirm(false)}>
              Cancel
            </Button>
            <Button
              variant="destructive"
              disabled={remove.isPending}
              onClick={() =>
                remove.mutate(crawl.id, {
                  onSuccess: () => {
                    setConfirm(false)
                    onDeleted()
                  },
                  onError: (e) => toast.error(e.message),
                })
              }
            >
              Delete
            </Button>
          </DialogFooter>
        </DialogContent>
      </Dialog>
    </div>
  )
}
