<?php
declare(strict_types=1);

namespace App\Jobs\Process;

/** OS-specific bits of the job runner (docs/PRD.md §9.3). */
interface ProcessControl
{
    /** Starts `php worker.php` so it outlives the current (php-cgi) request. */
    public function spawnDetached(string $phpCli, string $script): void;

    /** Command prefix that puts the child in its own process group, or []. */
    public function commandPrefix(): array;

    /** Asks the process tree to stop; $force kills it. */
    public function terminateTree(int $pid, bool $force): void;

    public function isAlive(int $pid): bool;
}
