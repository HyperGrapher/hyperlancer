import {
  createHashHistory,
  createRootRoute,
  createRoute,
  createRouter,
  type ErrorComponentProps,
  Link,
  lazyRouteComponent,
} from "@tanstack/react-router"

import { AppShell } from "@/components/layout/app-shell"
import { EmptyState } from "@/components/common/misc"
import { Button, buttonVariants } from "@/components/ui/button"
import { ProfileTab } from "@/features/site/profile-tab"
import { SiteLayout } from "@/features/site/site-layout"
import { sitesSearchSchema } from "@/lib/site-filters"
import { CompassIcon, TriangleAlertIcon } from "lucide-react"

function NotFound() {
  return (
    <EmptyState
      icon={<CompassIcon />}
      title="Page not found"
      description="This address doesn't match any page."
      action={
        <Link to="/" className={buttonVariants()}>
          Back to dashboard
        </Link>
      }
    />
  )
}

function RouteError({ error, reset }: ErrorComponentProps) {
  return (
    <div role="alert">
      <EmptyState
        icon={<TriangleAlertIcon />}
        title="This page could not be shown"
        description={
          (error instanceof Error && error.message) ||
          "An unexpected error occurred."
        }
        action={<Button onClick={reset}>Try again</Button>}
      />
    </div>
  )
}

function RoutePending() {
  return (
    <div
      role="status"
      aria-label="Loading"
      className="h-40 animate-pulse rounded-xl bg-muted/40"
    />
  )
}

const rootRoute = createRootRoute({
  component: AppShell,
  notFoundComponent: NotFound,
  errorComponent: RouteError,
})

const dashboardRoute = createRoute({
  getParentRoute: () => rootRoute,
  path: "/",
  component: lazyRouteComponent(
    () => import("@/features/dashboard/dashboard-page"),
    "DashboardPage"
  ),
})

const sitesRoute = createRoute({
  getParentRoute: () => rootRoute,
  path: "/sites",
  validateSearch: (search) => sitesSearchSchema.parse(search),
  component: lazyRouteComponent(
    () => import("@/features/sites/sites-page"),
    "SitesPage"
  ),
})

const siteRoute = createRoute({
  getParentRoute: () => rootRoute,
  path: "/sites/$siteId",
  params: {
    parse: (params) => ({ siteId: Number(params.siteId) }),
    stringify: (params) => ({ siteId: String(params.siteId) }),
  },
  component: SiteLayout,
})

const siteTabs = [
  createRoute({
    getParentRoute: () => siteRoute,
    path: "/",
    component: ProfileTab,
  }),
  createRoute({
    getParentRoute: () => siteRoute,
    path: "notes",
    component: lazyRouteComponent(
      () => import("@/features/site/notes-tab"),
      "NotesTab"
    ),
  }),
  createRoute({
    getParentRoute: () => siteRoute,
    path: "todos",
    component: lazyRouteComponent(
      () => import("@/features/site/todos-tab"),
      "TodosTab"
    ),
  }),
  createRoute({
    getParentRoute: () => siteRoute,
    path: "content",
    component: lazyRouteComponent(
      () => import("@/features/site/content/content-tab"),
      "ContentTab"
    ),
  }),
  createRoute({
    getParentRoute: () => siteRoute,
    path: "history",
    component: lazyRouteComponent(
      () => import("@/features/site/history-tab"),
      "HistoryTab"
    ),
  }),
  createRoute({
    getParentRoute: () => siteRoute,
    path: "activity",
    component: lazyRouteComponent(
      () => import("@/features/site/activity-tab"),
      "ActivityTab"
    ),
  }),
]

const todosRoute = createRoute({
  getParentRoute: () => rootRoute,
  path: "/todos",
  component: lazyRouteComponent(
    () => import("@/features/todos/todos-page"),
    "TodosPage"
  ),
})

const jobsRoute = createRoute({
  getParentRoute: () => rootRoute,
  path: "/jobs",
  component: lazyRouteComponent(
    () => import("@/features/jobs/jobs-page"),
    "JobsPage"
  ),
})

const settingsRoute = createRoute({
  getParentRoute: () => rootRoute,
  path: "/settings",
  component: lazyRouteComponent(
    () => import("@/features/settings/settings-page"),
    "SettingsPage"
  ),
})

const routeTree = rootRoute.addChildren([
  dashboardRoute,
  sitesRoute,
  siteRoute.addChildren(siteTabs),
  todosRoute,
  jobsRoute,
  settingsRoute,
])

// History mode needs the index.php fallback on Appytizer (docs/DECISIONS.md D9).
// VITE_ROUTER_MODE=hash switches to /#/sites/1 style URLs that work anywhere.
export const router = createRouter({
  routeTree,
  history:
    import.meta.env.VITE_ROUTER_MODE === "hash"
      ? createHashHistory()
      : undefined,
  defaultPreload: "intent",
  defaultPendingComponent: RoutePending,
  defaultPendingMs: 200,
  scrollRestoration: true,
})

declare module "@tanstack/react-router" {
  interface Register {
    router: typeof router
  }
}
