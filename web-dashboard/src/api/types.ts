// Shapes of the API responses (docs/PRD.md §8.2, §8.3).
export type JobStatus =
  "queued" | "running" | "succeeded" | "partial" | "failed" | "canceled"

export interface Job {
  id: number
  type: "profile" | "crawl" | "archive" | "backup" | "selfcheck"
  siteId: number | null
  siteKey?: string | null
  status: JobStatus
  progress: {
    current: number | null
    total: number | null
    label: string | null
  }
  stats: Record<string, number> | null
  error: string | null
  cancelRequested: boolean
  createdAt: string
  startedAt: string | null
  finishedAt: string | null
  result: unknown
}

export interface Health {
  ok: boolean
  version: string
  env: {
    os: string
    php: string
    phpSapi: string
    extensions: Record<string, boolean>
    sqlite: string
    fts5: boolean
    dataDir: string
    dataDirWritable: boolean
    python: { path: string; version: string | null; ok: boolean }
    phpCli: { path: string; ok: boolean }
  }
  worker: { idle: boolean; queued: number }
  migrations: { current: number; latest: number }
  legacyData: string[]
}

export interface BackupInfo {
  file: string
  bytes: number
  createdAt: string
}

// ---- sites, snapshots, taxonomy (docs/PRD.md §7.3, §8.2) -----------------------------------------------------

export type SiteStatus = "none" | "design" | "waiting" | "done" | "canceled"
export type Outcome = "ok" | "partial" | "failed"

export interface TagRef {
  id: number
  name: string
  color: string | null
}

export interface CategoryRef {
  id: number
  name: string
  color: string | null
}

export interface SiteListItem {
  id: number
  siteKey: string
  url: string
  name: string | null
  faviconUrl: string | null
  previewUrl: string | null
  status: SiteStatus
  category: CategoryRef | null
  tags: TagRef[]
  cms: string | null
  cmsVersion: string | null
  server: string | null
  hosting: string | null
  lastFetchedAt: string | null
  lastFetchOutcome: Outcome | null
  tlsValidUntil: string | null
  domainExpiresOn: string | null
  openTodos: number
  deadline: string | null
  archivedAt: string | null
  updatedAt: string
}

export interface SnapshotMeta {
  id: number
  siteId: number
  fetchedAt: string
  outcome: Outcome
  durationMs: number | null
  httpStatus: number | null
  errorSummary: string | null
  changesCount: number | null
  schemaVersion: number
  cms: string | null
  cmsVersion: string | null
  ip: string | null
  tlsValidUntil: string | null
}

export interface SiteDetail extends SiteListItem {
  registrableDomain: string | null
  finalUrl: string | null
  title: string | null
  registrar: string | null
  ipAddress: string | null
  allowPrivateNet: boolean
  createdAt: string
  latestSnapshot: SnapshotMeta | null
  lastAttempt: SnapshotMeta | null
  activeJob: Job | null
  contacts: {
    id: number
    name: string
    role: string | null
    email: string | null
    phone: string | null
    preferredChannel: string | null
  }[]
  links: { id: number; kind: string; label: string | null; url: string }[]
  counts: {
    notes: number
    todosOpen: number
    todosDone: number
    crawls: number
    snapshots: number
  }
}

export interface SiteList {
  items: SiteListItem[]
  total: number
  page: number
  perPage: number
  facets: Record<
    "status" | "cms" | "category" | "tag" | "outcome",
    Record<string, number>
  >
}

export interface Technology {
  category: string
  name: string
  version: string | null
  confidence: "low" | "medium" | "high"
  evidence: string[]
}

export interface ProfileSummary {
  schemaVersion: number
  outcome: Outcome
  errors: { section: string; code: string; message: string }[]
  url: {
    requested: string | null
    final: string | null
    redirects: { status: number; from: string; to: string }[]
  }
  http: {
    status: number | null
    ttfbMs: number | null
    totalMs: number | null
    htmlBytes: number | null
    compression: string | null
    httpVersion: string | null
    httpsRedirect: boolean | null
    wwwCanonical: string | null
    wwwRedirect: boolean | null
    blocked: boolean
    blockedBy: string | null
    error: string | null
  }
  tech: Technology[]
  cms: {
    name: string | null
    version: string | null
    theme: string | null
    plugins: string[]
  }
  hosting: {
    ips: string[]
    ipv6: string[]
    org: string | null
    asn: string | null
    prefix: string | null
    country: string | null
    reverseDns: string | null
    edge: string | null
    server: string | null
    language: string | null
    languageVersion: string | null
  }
  dns: {
    ns: string[]
    nsSource: "dns" | "registry" | null
    mx: { pref: number; host: string }[]
    emailProvider: string | null
    spf: string | null
    dmarc: string | null
    dnssec: boolean
    caa: string[]
    txt: string[]
  }
  tls: {
    verified: boolean
    issuer: string | null
    validFrom: string | null
    validUntil: string | null
    daysLeft: number | null
    sanCount: number
    sans: string[]
    fingerprint: string | null
    error: string | null
  }
  domain: {
    registrar: string | null
    registrant: string | null
    created: string | null
    expires: string | null
    updated: string | null
    ageYears: number | null
    statuses: string[]
    transferLocked: boolean | null
    method: string | null
    source: string | null
  }
  seo: {
    title: string | null
    titleLength: number
    description: string | null
    descriptionLength: number
    canonical: string | null
    robots: string | null
    noindex: boolean
    lang: string | null
    hreflang: { lang: string; url: string }[]
    ogTitle: string | null
    ogImage: string | null
    og: Record<string, string | null>
    twitter: Record<string, string | null>
    structuredData: string[]
    h1Count: number
    headings: { level: number; text: string }[]
    imagesTotal: number
    imagesMissingAlt: number
    links: { internal: number; external: number; nofollow: number } | null
    consentTool: string | null
    favicon: string | null
    parseable: boolean
  }
  sitemaps: {
    robotsStatus: number | null
    disallowRules: number
    allowRules: number
    crawlDelay: number | null
    files: {
      url: string
      kind: string
      urls: number | null
      error: string | null
      warning: string | null
    }[]
    totalUrls: number
    urls: { loc: string; lastmod: string | null }[]
    truncated: boolean
  }
  contact: {
    emails: string[]
    phones: string[]
    address: string | null
    socials: { network: string; url: string }[]
    whatsapp: string[]
  }
  security: {
    present: string[]
    missing: string[]
    cookies: {
      name: string
      secure: boolean
      httpOnly: boolean
      sameSite: string | null
    }[]
  }
  archive: {
    first: string | null
    firstUrl: string | null
    last: string | null
    lastUrl: string | null
  }
  design: {
    estimatedFrom: string | null
    latestAsset: string | null
    confidence: "low" | "medium" | null
    basis: string[]
    clues: {
      kind: string
      label: string
      date: string
      detail: string | null
      strength: "strong" | "medium" | "weak" | "context"
    }[]
  }
  limitations: string[]
}

export interface Snapshot extends SnapshotMeta {
  summary: ProfileSummary
  raw?: unknown
}

export interface DiffChange {
  path: string
  label: string
  kind: "added" | "removed" | "changed"
  before: string | null
  after: string | null
}

export interface SnapshotDiffResult {
  a: SnapshotMeta | null
  b: SnapshotMeta
  groups: { section: string; changes: DiffChange[] }[]
  unchangedSections: string[]
  count: number
}

export interface Category extends CategoryRef {
  sortOrder: number
  siteCount?: number
}

export interface Tag extends TagRef {
  siteCount?: number
}

export interface SavedView {
  id: number
  name: string
  query: string
  sortOrder: number
}

// ---- notes, to-dos, activity, dashboard ------------------------------------------------------------------------------

export interface NoteMeta {
  id: number
  siteId: number
  title: string
  pinned: boolean
  createdAt: string
  updatedAt: string
  excerpt?: string
}

export interface Note extends NoteMeta {
  bodyMd: string
}

export type Priority = "low" | "normal" | "high"

export interface Todo {
  id: number
  siteId: number
  title: string
  detail: string | null
  done: boolean
  doneAt: string | null
  dueDate: string | null
  priority: Priority
  sortOrder: number
  createdAt: string
  updatedAt: string
}

export interface TodoWithSite extends Todo {
  site: { id: number; siteKey: string; name: string | null }
}

export interface ActivityEntry {
  id: number
  siteId: number | null
  siteKey: string | null
  type: string
  message: string
  createdAt: string
  data?: Record<string, unknown> | null
}

export interface Contact {
  id: number
  siteId: number
  name: string
  role: string | null
  email: string | null
  phone: string | null
  preferredChannel: string | null
  notes: string | null
}

export interface SiteLinkItem {
  id: number
  siteId: number
  kind: string
  label: string | null
  url: string
}

export interface DashboardData {
  today: string
  statusCounts: Record<SiteStatus, number>
  siteCount: number
  deadlines: {
    id: number
    siteKey: string
    name: string | null
    deadline: string
    status: SiteStatus
  }[]
  openTodos: Omit<
    TodoWithSite,
    "detail" | "doneAt" | "sortOrder" | "createdAt" | "updatedAt"
  >[]
  openTodosTotal: number
  expiring: {
    tls: { id: number; siteKey: string; name: string | null; date: string }[]
    domains: {
      id: number
      siteKey: string
      name: string | null
      date: string
    }[]
  }
  recentFetches: {
    id: number
    siteKey: string
    name: string | null
    fetchedAt: string
    outcome: Outcome | null
    cms: string | null
  }[]
  jobs: Job[]
  activity: ActivityEntry[]
}

// ---- crawls ---------------------------------------------------------------------------------------------------------

export type CrawlStatus =
  | "queued"
  | "running"
  | "canceling"
  | "done"
  | "partial"
  | "failed"
  | "canceled"

export interface CrawlOptions {
  startUrls: string[]
  maxPages: number
  maxDepth: number
  includeSubdomains: boolean
  include: string[]
  exclude: string[]
  respectRobots: boolean
  delay: number
  concurrency: number
  types: { images: boolean; documents: boolean; media: boolean; fonts: boolean }
  extensions?: string[]
  maxFileMb: number
  maxTotalMb: number
  mainSelector?: string
  strip: string[]
  keepChrome: boolean
  languages: string[]
  wordpress: "auto" | "off" | "only"
}

export interface Crawl {
  id: number
  siteId: number
  jobId: number | null
  status: CrawlStatus
  outputDir: string
  path: string
  options: CrawlOptions
  pages: number
  images: number
  files: number
  bytes: number
  archiveReady: boolean
  startedAt: string
  finishedAt: string | null
  stopReason: string | null
  job: Job | null
}

export interface CrawlDetail extends Crawl {
  manifest: {
    errors?: { url?: string; message?: string }[]
    counts?: Record<string, number>
    stop_reason?: string | null
  } | null
}

export interface CrawlPageItem {
  id: string
  url: string
  file: string
  title: string
  depth: number
  wordCount: number
  images: number
  lang: string | null
}

export interface CrawlTreeNode {
  name: string
  page: string | null
  children: CrawlTreeNode[]
}

export interface CrawlPage {
  meta: {
    id: string
    url: string
    file: string
    title: string
    description?: string
    word_count: number
    headings: { level: number; text: string }[]
    images: string[]
    files: string[]
    links_internal: string[]
    links_external: string[]
  }
  markdown: string
}

export interface CrawlAsset {
  id: string
  kind: "image" | "file" | "media"
  url: string
  file: string
  bytes: number
  mime: string
  width: number | null
  height: number | null
  altTexts: string[]
  usedOn: string[]
}
