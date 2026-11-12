import { Link } from "@tanstack/react-router"
import {
  CircleCheckIcon,
  CircleDotIcon,
  DownloadIcon,
  GlobeIcon,
  ListTodoIcon,
  NotebookPenIcon,
  RefreshCwIcon,
  TagIcon,
  UserIcon,
  WorkflowIcon,
} from "lucide-react"

import type { ActivityEntry } from "@/api/types"
import { relativeTime } from "@/lib/format"

const icons: Record<string, typeof GlobeIcon> = {
  "status.changed": WorkflowIcon,
  "category.changed": TagIcon,
  "tags.changed": TagIcon,
  "profile.fetched": RefreshCwIcon,
  "snapshot.deleted": RefreshCwIcon,
  "note.created": NotebookPenIcon,
  "note.updated": NotebookPenIcon,
  "note.deleted": NotebookPenIcon,
  "todo.created": ListTodoIcon,
  "todo.completed": CircleCheckIcon,
  "todo.reopened": CircleDotIcon,
  "todo.deleted": ListTodoIcon,
  "contact.created": UserIcon,
  "contact.updated": UserIcon,
  "contact.deleted": UserIcon,
  "crawl.started": DownloadIcon,
  "site.created": GlobeIcon,
}

export function ActivityFeed({
  items,
  showSite = false,
}: {
  items: ActivityEntry[]
  showSite?: boolean
}) {
  return (
    <ol className="flex flex-col">
      {items.map((item) => {
        const Icon = icons[item.type] ?? GlobeIcon
        return (
          <li key={item.id} className="flex gap-3 py-2">
            <span className="mt-0.5 flex size-6 shrink-0 items-center justify-center rounded-full bg-muted text-muted-foreground">
              <Icon className="size-3.5" />
            </span>
            <div className="min-w-0 flex-1 text-sm">
              <p className="break-words">{item.message}</p>
              <p className="text-xs text-muted-foreground">
                {showSite && item.siteId !== null && item.siteKey && (
                  <>
                    <Link
                      to="/sites/$siteId/activity"
                      params={{ siteId: item.siteId }}
                      className="hover:text-foreground hover:underline"
                    >
                      {item.siteKey}
                    </Link>
                    {" · "}
                  </>
                )}
                <span title={item.createdAt}>
                  {relativeTime(item.createdAt)}
                </span>
              </p>
            </div>
          </li>
        )
      })}
    </ol>
  )
}
