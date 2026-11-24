# fork-sync

This branch only holds the workflow that keeps this fork's `main` in step with
[mattgodbolt/xania](https://github.com/mattgodbolt/xania): every six hours it asks GitHub to sync `main` from
upstream (the same as the **Sync fork** button). It's the fork's default branch because scheduled workflows only
run from the default branch.

The code is on [`main`](../../tree/main), which stays an exact mirror of upstream. Branch from `main` for changes.

Run it by hand from **Actions → Sync main from upstream → Run workflow**.
