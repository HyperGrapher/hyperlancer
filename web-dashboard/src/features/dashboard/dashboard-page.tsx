import { Link } from "@tanstack/react-router"
import { GlobeIcon, LockIcon } from "lucide-react"
import type { ReactNode } from "react"
import { cn } from "cn"

import { useDashboard } from "@/api/hooks/work"
import {
  JobStatusBadge,
  OutcomeBadge,
  StatusDot,
} from "@/components/common/badges"
import { PageHeader } from "@/components/common/misc"
import { Progress } from "@/components/ui/progress"
import { ActivityFeed } from "@/features/activity/activity-feed"
import { TodoList } from "@/features/todos/todo-list"
import { daysUntil, relativeTime, shortDate } from "@/lib/format"
import { STATUSES } from "@/lib/domain"

function Widget({
  title,
  action,
  children,
  className,
}: {
  title: string
  action?: ReactNode
  children: ReactNode
  className?: string
}) {
  return (
    <section
      className={cn(
        "rounded-xl border border-border bg-card/60 p-4",
        className
      )}
    >
      <div className="mb-3 flex items-center justify-between">
        <h2 className="text-sm font-medium">{title}</h2>
        {action}
      </div>
      {children}
    </section>
  )
}

const moreLink = "text-xs text-muted-foreground hover:text-foreground"
const Empty = ({ children }: { children: ReactNode }) => (
  <p className="text-sm text-muted-foreground">{children}</p>
)

export function DashboardPage() {
  const { data, error, isLoading } = useDashboard()

  if (error) return <p className="text-sm text-destructive">{error.message}</p>
  if (isLoading || !data)
    return <div className="h-64 animate-pulse rounded-xl bg-muted/40" />

  const expiring = [
    ...data.expiring.tls.map((item) => ({
      ...item,
      kind: "SSL" as const,
      days: daysUntil(item.date) ?? 0,
    })),
    ...data.expiring.domains.map((item) => ({
      ...item,
      kind: "Domain" as const,
      days: daysUntil(item.date) ?? 0,
    })),
  ].sort((a, b) => a.days - b.days)

  return (
    <div>
      <PageHeader
        title="Dashboard"
        description={
          data.siteCount === 0
            ? "Add your first site to get started."
            : "What needs your attention today."
        }
      />

      <div className="grid gap-4 md:grid-cols-2 xl:grid-cols-3">
        <Widget
          title="Projects by status"
          action={
            <Link to="/sites" className={moreLink}>
              All sites
            </Link>
          }
        >
          <div className="grid grid-cols-2 gap-2">
            {STATUSES.map((status) => (
              <Link
                key={status}
                to="/sites"
                search={{ status }}
                className="flex items-center justify-between rounded-lg border border-border px-3 py-2 text-sm hover:bg-muted/60"
              >
                <span className="flex items-center gap-2">
                  <StatusDot status={status} />
                  {status}
                </span>
                <span className="font-semibold">
                  {data.statusCounts[status]}
                </span>
              </Link>
            ))}
          </div>
        </Widget>

        <Widget title="Upcoming deadlines" className="border-sky-500/30">
          {data.deadlines.length === 0 ? (
            <Empty>Nothing due in the next two weeks.</Empty>
          ) : (
            <ul className="flex flex-col gap-2">
              {data.deadlines.map((site) => {
                const days = daysUntil(site.deadline) ?? 0
                const tone =
                  days < 0
                    ? {
                        bar: "border-l-rose-500",
                        chip: "bg-rose-500/15 text-rose-700 dark:text-rose-300",
                        text: "overdue",
                      }
                    : days <= 3
                      ? {
                          bar: "border-l-amber-500",
                          chip: "bg-amber-500/15 text-amber-700 dark:text-amber-300",
                          text: "soon",
                        }
                      : {
                          bar: "border-l-sky-500",
                          chip: "bg-sky-500/15 text-sky-700 dark:text-sky-300",
                          text: "upcoming",
                        }
                return (
                  <li key={site.id}>
                    <Link
                      to="/sites/$siteId"
                      params={{ siteId: site.id }}
                      className={cn(
                        "flex items-center gap-3 rounded-lg border border-l-4 border-border bg-card px-3 py-2 shadow-sm hover:bg-muted/60",
                        tone.bar
                      )}
                    >
                      <span className="min-w-0 flex-1">
                        <span className="block truncate font-semibold">
                          {site.siteKey}
                        </span>
                        <span className="block text-xs text-muted-foreground">
                          {shortDate(site.deadline)}
                        </span>
                      </span>
                      <span
                        className={cn(
                          "shrink-0 rounded-md px-2 py-1 text-xs font-semibold",
                          tone.chip
                        )}
                      >
                        {days < 0
                          ? `${-days}d overdue`
                          : days === 0
                            ? "Today"
                            : days === 1
                              ? "Tomorrow"
                              : `in ${days}d`}
                      </span>
                    </Link>
                  </li>
                )
              })}
            </ul>
          )}
        </Widget>

        <Widget title="Expiring soon">
          {expiring.length === 0 ? (
            <Empty>
              No SSL certificate or domain expires in the next 30 to 60 days.
            </Empty>
          ) : (
            <ul className="flex flex-col gap-1">
              {expiring.map((item) => (
                <li key={`${item.id}-${item.kind}`}>
                  <Link
                    to="/sites/$siteId"
                    params={{ siteId: item.id }}
                    className="flex items-center gap-2 rounded-lg px-2 py-1.5 text-sm hover:bg-muted/60"
                  >
                    {item.kind === "SSL" ? (
                      <LockIcon className="size-4 text-muted-foreground" />
                    ) : (
                      <GlobeIcon className="size-4 text-muted-foreground" />
                    )}
                    <span className="flex-1 truncate">{item.siteKey}</span>
                    <span className="text-xs text-muted-foreground">
                      {item.kind}
                    </span>
                    <span
                      className={cn(
                        "w-14 text-right",
                        item.days < 14
                          ? "text-rose-600 dark:text-rose-300"
                          : "text-amber-600 dark:text-amber-300"
                      )}
                    >
                      {item.days < 0 ? "expired" : `${item.days}d`}
                    </span>
                  </Link>
                </li>
              ))}
            </ul>
          )}
        </Widget>

        <Widget
          title="Open to-dos (overdue & this week)"
          className="md:col-span-2"
          action={
            <Link to="/todos" className={moreLink}>
              All to-dos ({data.openTodosTotal})
            </Link>
          }
        >
          {data.openTodos.length === 0 ? (
            <Empty>Nothing overdue or due this week.</Empty>
          ) : (
            <TodoList todos={data.openTodos} showSite />
          )}
        </Widget>

        <Widget title="Recently fetched">
          {data.recentFetches.length === 0 ? (
            <Empty>No profile has been fetched yet.</Empty>
          ) : (
            <ul className="flex flex-col gap-1">
              {data.recentFetches.map((site) => (
                <li key={site.id}>
                  <Link
                    to="/sites/$siteId"
                    params={{ siteId: site.id }}
                    className="flex items-center gap-2 rounded-lg px-2 py-1.5 text-sm hover:bg-muted/60"
                  >
                    <span className="flex-1 truncate">{site.siteKey}</span>
                    <span className="text-xs text-muted-foreground">
                      {relativeTime(site.fetchedAt)}
                    </span>
                    <OutcomeBadge outcome={site.outcome} />
                  </Link>
                </li>
              ))}
            </ul>
          )}
        </Widget>

        <Widget
          title="Jobs"
          action={
            <Link to="/jobs" className={moreLink}>
              Job center
            </Link>
          }
        >
          {data.jobs.length === 0 ? (
            <Empty>No jobs in the last day.</Empty>
          ) : (
            <ul className="flex flex-col gap-3">
              {data.jobs.map((job) => (
                <li key={job.id} className="flex flex-col gap-1.5 text-sm">
                  <div className="flex items-center justify-between gap-2">
                    <span className="truncate">
                      {job.type} #{job.id}
                    </span>
                    <JobStatusBadge status={job.status} />
                  </div>
                  {job.status === "running" && job.progress.total ? (
                    <Progress
                      value={
                        ((job.progress.current ?? 0) / job.progress.total) * 100
                      }
                    />
                  ) : null}
                </li>
              ))}
            </ul>
          )}
        </Widget>

        <Widget title="Activity" className="md:col-span-2">
          {data.activity.length === 0 ? (
            <Empty>Changes you make show up here.</Empty>
          ) : (
            <ActivityFeed items={data.activity.slice(0, 8)} showSite />
          )}
        </Widget>
      </div>
    </div>
  )
}
