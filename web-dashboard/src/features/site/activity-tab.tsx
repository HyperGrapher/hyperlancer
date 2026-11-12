import { getRouteApi } from "@tanstack/react-router"
import { ActivityIcon } from "lucide-react"

import { useActivity } from "@/api/hooks/work"
import { EmptyState } from "@/components/common/misc"
import { Button } from "@/components/ui/button"
import { ActivityFeed } from "@/features/activity/activity-feed"

const routeApi = getRouteApi("/sites/$siteId")

export function ActivityTab() {
  const { siteId } = routeApi.useParams()
  const {
    data,
    error,
    isLoading,
    hasNextPage,
    fetchNextPage,
    isFetchingNextPage,
  } = useActivity(siteId)
  const items = data?.pages.flatMap((page) => page.items) ?? []

  if (isLoading)
    return <div className="h-24 animate-pulse rounded-xl bg-muted/40" />
  if (error) return <p className="text-sm text-destructive">{error.message}</p>
  if (items.length === 0)
    return <EmptyState icon={<ActivityIcon />} title="No activity yet" />

  return (
    <div className="rounded-xl border border-border bg-card/60 px-4 py-2">
      <ActivityFeed items={items} />
      {hasNextPage && (
        <div className="py-2">
          <Button
            variant="outline"
            size="sm"
            disabled={isFetchingNextPage}
            onClick={() => fetchNextPage()}
          >
            {isFetchingNextPage ? "Loading…" : "Load more"}
          </Button>
        </div>
      )}
    </div>
  )
}
