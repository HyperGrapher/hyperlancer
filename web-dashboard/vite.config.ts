import { cp, mkdir, writeFile } from "node:fs/promises"
import { resolve, sep } from "node:path"
import tailwindcss from "@tailwindcss/vite"
import react from "@vitejs/plugin-react"
import { defineConfig, type Plugin } from "vite"

const projectRoot = import.meta.dirname
const distDir = resolve(projectRoot, "dist")

// Everything under backend/ is shipped except tests, dev tooling, runtime data and local config.
const BACKEND_EXCLUDE = [
  "tests",
  "vendor",
  "data",
  "composer.json",
  "composer.lock",
  "phpunit.xml",
  "dev-router.php",
  "build-info.php",
]

function copyPhpBackend(): Plugin {
  return {
    name: "copy-php-backend",
    apply: "build",
    async writeBundle() {
      // The data dir must never be inside dist/: Vite empties dist/ on every build (docs/DECISIONS.md D10).
      const dataDir = resolve(
        process.env.APP_DATA_DIR ?? resolve(projectRoot, "data")
      )
      if (dataDir === distDir || dataDir.startsWith(distDir + sep)) {
        throw new Error(
          `Refusing to build: the data dir (${dataDir}) is inside dist/.`
        )
      }

      const source = resolve(projectRoot, "backend")
      const target = resolve(distDir, "backend")
      await mkdir(target, { recursive: true })
      await cp(source, target, {
        recursive: true,
        filter: (file) => {
          const name = file.slice(source.length + 1).split(sep)[0]
          return !BACKEND_EXCLUDE.includes(name) && !name.endsWith(".local.php")
        },
      })

      const info = {
        project_root: projectRoot,
        built_at: new Date().toISOString(),
        version: process.env.npm_package_version ?? "dev",
      }
      await writeFile(
        resolve(target, "build-info.php"),
        `<?php\nreturn ${phpExport(info)};\n`
      )
    },
  }
}

function phpExport(value: Record<string, string>): string {
  const entries = Object.entries(value).map(
    ([k, v]) =>
      `    '${k}' => '${v.replace(/\\/g, "\\\\").replace(/'/g, "\\'")}',`
  )
  return `[\n${entries.join("\n")}\n]`
}

// https://vite.dev/config/
export default defineConfig({
  plugins: [react(), tailwindcss(), copyPhpBackend()],
  server: {
    proxy: {
      "/api": {
        target: process.env.PHP_API_TARGET ?? "http://127.0.0.1:8787",
        changeOrigin: true,
        secure: false,
      },
    },
  },
  resolve: {
    alias: {
      "@": resolve(import.meta.dirname, "./src"),
    },
  },
})
