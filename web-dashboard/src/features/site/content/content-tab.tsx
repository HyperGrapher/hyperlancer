import { getRouteApi } from "@tanstack/react-router"
import { DownloadCloudIcon, PlusIcon } from "lucide-react"
import { useState } from "react"
import { cn } from "cn"

import { crawlActive, useCrawl, useCrawls } from "@/api/hooks/crawls"
import { useSite } from "@/api/hooks/sites"
import type { Crawl } from "@/api/types"
import { CrawlStatusBadge } from "@/components/common/badges"
import { EmptyState } from "@/components/common/misc"
import { Button } from "@/components/ui/button"
import { formatBytes, relativeTime } from "@/lib/format"

import { CrawlProgress } from "./crawl-progress"
import { CrawlResult } from "./crawl-result"
import { DEFAULT_OPTIONS, NewCrawlDialog } from "./new-crawl-dialog"

const routeApi = getRouteApi("/sites/$siteId")

function Selected({ id, onDeleted }: { id: number; onDeleted: () => void }) {
  const { data: crawl, error } = useCrawl(id)
  if (error) return <p className="text-sm text-destructive">{error.message}</p>
  if (!crawl)
    return <div className="h-40 animate-pulse rounded-xl bg-muted/40" />
  return (
    <div className="grid gap-4">
      <div className="flex flex-wrap items-center gap-2">
        <h3 className="text-lg font-semibold">
          Crawl of {new Date(crawl.startedAt).toLocaleString()}
        </h3>
        <CrawlStatusBadge status={crawl.status} />
      </div>
      {crawlActive(crawl) ? (
        <CrawlProgress key={crawl.id} crawl={crawl} />
      ) : (
        <CrawlResult crawl={crawl} onDeleted={onDeleted} />
      )}
    </div>
  )
}

export function ContentTab() {
  const { siteId } = routeApi.useParams()
  const { data: site } = useSite(siteId)
  const { data: crawls = [], isLoading, error } = useCrawls(siteId)
  const [picked, setPicked] = useState<number | null>(null)
  const [dialog, setDialog] = useState(false)

  // Follow a running crawl by default, otherwise the newest one.
  const selected =
    picked !== null && crawls.some((c) => c.id === picked)
      ? picked
      : (crawls.find(crawlActive)?.id ?? crawls[0]?.id ?? null)
  const busy = crawls.some(crawlActive)

  if (isLoading)
    return <div className="h-40 animate-pulse rounded-xl bg-muted/40" />
  if (error) return <p className="text-sm text-destructive">{error.message}</p>

  const newButton = (
    <Button size="sm" disabled={busy || !site} onClick={() => setDialog(true)}>
      <PlusIcon /> New crawl
    </Button>
  )
  return (
    <div className="grid gap-4">
      {crawls.length === 0 ? (
        <EmptyState
          icon={<DownloadCloudIcon />}
          title="No content downloaded yet"
          description="Crawl the site to save every page as Markdown with its images and files."
          action={newButton}
        />
      ) : (
        <div className="grid gap-4 lg:grid-cols-[14rem_minmax(0,1fr)]">
          <div className="grid content-start gap-2">
            {newButton}
            {busy && (
              <p className="text-xs text-muted-foreground">
                A crawl is already queued or running.
              </p>
            )}
            <ul className="grid gap-1.5">
              {crawls.map((c: Crawl) => (
                <li key={c.id}>
                  <button
                    type="button"
                    onClick={() => setPicked(c.id)}
                    className={cn(
                      "w-full rounded-lg border p-2.5 text-left text-sm hover:bg-accent",
                      c.id === selected && "border-primary/50 bg-accent"
                    )}
                  >
                    <div className="flex items-center justify-between gap-2">
                      <span>{relativeTime(c.startedAt)}</span>
                      <CrawlStatusBadge status={c.status} />
                    </div>
                    <div className="mt-1 text-xs text-muted-foreground">
                      {c.pages} pages · {c.images} images · {c.files} files ·{" "}
                      {formatBytes(c.bytes)}
                    </div>
                  </button>
                </li>
              ))}
            </ul>
          </div>
          <div className="min-w-0">
            {selected !== null && (
              <Selected
                key={selected}
                id={selected}
                onDeleted={() => setPicked(null)}
              />
            )}
          </div>
        </div>
      )}
      {dialog && site && (
        <NewCrawlDialog
          siteId={siteId}
          siteUrl={site.url}
          initial={{ ...DEFAULT_OPTIONS, ...(crawls[0]?.options ?? {}) }}
          onClose={() => setDialog(false)}
          onStarted={(id) => setPicked(id)}
        />
      )}
    </div>
  )
}
