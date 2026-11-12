import { Link, Outlet, useNavigate } from "@tanstack/react-router"
import {
  BookmarkIcon,
  BriefcaseIcon,
  GlobeIcon,
  LayoutDashboardIcon,
  ListTodoIcon,
  LoaderCircleIcon,
  MenuIcon,
  MoonIcon,
  PlusIcon,
  SearchIcon,
  SettingsIcon,
  SunIcon,
  WorkflowIcon,
  XIcon,
} from "lucide-react"
import { useState, type FormEvent } from "react"

import { useActiveJobs } from "@/api/hooks/jobs"
import { useTaxonomyMutations, useViews } from "@/api/hooks/sites"
import type { SitesSearch } from "@/lib/site-filters"
import { toast } from "sonner"
import { AddSiteDialog } from "@/features/sites/add-site-dialog"
import { JobWatcher } from "@/features/jobs/job-watcher"
import { useTheme } from "@/components/theme-provider"
import { Button } from "@/components/ui/button"
import { Input } from "@/components/ui/input"
import { Sheet, SheetContent, SheetTitle } from "@/components/ui/sheet"

const nav = [
  { to: "/", label: "Dashboard", icon: LayoutDashboardIcon, exact: true },
  { to: "/sites", label: "Sites", icon: GlobeIcon, exact: false },
  { to: "/todos", label: "To-dos", icon: ListTodoIcon, exact: false },
  { to: "/jobs", label: "Jobs", icon: WorkflowIcon, exact: false },
] as const

const linkBase =
  "flex items-center gap-2.5 rounded-lg px-2.5 py-1.5 text-sm text-muted-foreground transition-colors hover:bg-muted/60 hover:text-foreground [&_svg]:size-4"
const linkActive = "bg-muted text-foreground font-medium"

function SidebarContent({ onNavigate }: { onNavigate?: () => void }) {
  const views = useViews().data ?? []
  const { remove } = useTaxonomyMutations("views")
  return (
    <div className="flex h-full flex-col gap-6 p-3">
      <Link
        to="/"
        className="flex items-center gap-2 px-2 pt-1"
        onClick={onNavigate}
      >
        <span className="flex size-7 items-center justify-center rounded-lg bg-primary text-primary-foreground">
          <BriefcaseIcon className="size-4" />
        </span>
        <span className="font-semibold tracking-tight">Freelance</span>
      </Link>

      <nav className="flex flex-col gap-0.5">
        {nav.map((item) => (
          <Link
            key={item.to}
            to={item.to}
            className={linkBase}
            activeProps={{ className: linkActive }}
            activeOptions={{ exact: item.exact, includeSearch: false }}
            onClick={onNavigate}
          >
            <item.icon />
            {item.label}
          </Link>
        ))}
      </nav>

      <div>
        <p className="px-2.5 pb-1.5 text-xs font-medium tracking-wide text-muted-foreground/70 uppercase">
          Saved views
        </p>
        <div className="flex flex-col gap-0.5">
          {views.length === 0 && (
            <p className="px-2.5 text-xs text-muted-foreground">
              Filter the sites list, then use “Save view”.
            </p>
          )}
          {views.map((view) => (
            <div key={view.id} className="group relative">
              <Link
                to="/sites"
                search={
                  Object.fromEntries(
                    new URLSearchParams(view.query)
                  ) as SitesSearch
                }
                className={linkBase}
                activeProps={{ className: linkActive }}
                activeOptions={{ includeSearch: true }}
                onClick={onNavigate}
              >
                <BookmarkIcon />
                <span className="truncate">{view.name}</span>
              </Link>
              <button
                type="button"
                aria-label={`Delete view ${view.name}`}
                className="absolute top-1/2 right-1.5 hidden -translate-y-1/2 rounded p-1 text-muted-foreground group-hover:block hover:bg-background hover:text-foreground"
                onClick={() =>
                  remove.mutate(view.id, {
                    onError: (e) => toast.error(e.message),
                  })
                }
              >
                <XIcon className="size-3" />
              </button>
            </div>
          ))}
        </div>
      </div>

      <div className="mt-auto">
        <Link
          to="/settings"
          className={linkBase}
          activeProps={{ className: linkActive }}
          onClick={onNavigate}
        >
          <SettingsIcon />
          Settings
        </Link>
      </div>
    </div>
  )
}

function ThemeToggle() {
  const { theme, setTheme } = useTheme()
  const dark =
    theme === "dark" ||
    (theme === "system" &&
      window.matchMedia("(prefers-color-scheme: dark)").matches)
  return (
    <Button
      variant="ghost"
      size="icon"
      onClick={() => setTheme(dark ? "light" : "dark")}
      aria-label="Toggle theme"
    >
      {dark ? <SunIcon /> : <MoonIcon />}
    </Button>
  )
}

export function AppShell() {
  const navigate = useNavigate()
  const [mobileOpen, setMobileOpen] = useState(false)
  const [addOpen, setAddOpen] = useState(false)
  const [query, setQuery] = useState("")
  const activeJobs = useActiveJobs().data?.items ?? []
  const runningJobs = activeJobs.length

  function search(event: FormEvent) {
    event.preventDefault()
    navigate({ to: "/sites", search: query ? { q: query } : {} })
  }

  return (
    <div className="flex min-h-screen bg-background">
      <aside className="sticky top-0 hidden h-screen w-60 shrink-0 border-r border-border bg-sidebar/40 lg:block">
        <SidebarContent />
      </aside>

      <Sheet open={mobileOpen} onOpenChange={setMobileOpen}>
        <SheetContent side="left" className="w-64 p-0">
          <SheetTitle className="sr-only">Navigation</SheetTitle>
          <SidebarContent onNavigate={() => setMobileOpen(false)} />
        </SheetContent>
      </Sheet>

      <div className="flex min-w-0 flex-1 flex-col">
        <header className="sticky top-0 z-30 flex h-14 items-center gap-2 border-b border-border bg-background/80 px-3 backdrop-blur sm:px-5">
          <Button
            variant="ghost"
            size="icon"
            className="lg:hidden"
            onClick={() => setMobileOpen(true)}
            aria-label="Open navigation"
          >
            <MenuIcon />
          </Button>
          <form onSubmit={search} className="relative max-w-md flex-1">
            <SearchIcon className="pointer-events-none absolute top-1/2 left-2.5 size-4 -translate-y-1/2 text-muted-foreground" />
            <Input
              value={query}
              onChange={(event) => setQuery(event.target.value)}
              placeholder="Search sites…"
              className="pl-8"
            />
          </form>
          <div className="ml-auto flex items-center gap-1">
            {runningJobs > 0 && (
              <Link
                to="/jobs"
                className="hidden items-center gap-1.5 rounded-lg px-2 py-1 text-xs text-sky-700 hover:bg-muted sm:flex dark:text-sky-300"
              >
                <LoaderCircleIcon className="size-3.5 animate-spin" />
                {runningJobs} {runningJobs === 1 ? "job" : "jobs"}
              </Link>
            )}
            <ThemeToggle />
            <Button onClick={() => setAddOpen(true)}>
              <PlusIcon />
              <span className="hidden sm:inline">Add site</span>
            </Button>
          </div>
        </header>

        <main className="mx-auto w-full max-w-7xl flex-1 px-3 py-6 sm:px-6">
          <Outlet />
        </main>
      </div>

      <AddSiteDialog open={addOpen} onOpenChange={setAddOpen} />
      <JobWatcher />
    </div>
  )
}
