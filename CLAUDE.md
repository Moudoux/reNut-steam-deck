# reNut (Steam Deck fork)

This repository is a fork of upstream [masterspike52/reNut](https://github.com/masterspike52/reNut)
(remote `upstream`), built and tuned only for the Steam Deck. Read
[docs/STEAM_DECK.md](docs/STEAM_DECK.md) before working here: it has the build,
deploy, profiling workflow, and the list of everything this fork changes.

## Rules

- **Rebasing/merging upstream must not lose any change listed in
  docs/STEAM_DECK.md ("What this fork changes")**, including the patches
  `deck/rexglue-sdk-v0.10.0-deck.patch` and `deck/xenosrecomp-deck.patch`. Drop one of ours only when upstream
  fixed the same problem, and update the doc when you do.
- Document every new fork change in docs/STEAM_DECK.md in the same commit.
- Build only with `deck/build.sh` (Ubuntu 24.04 container: the host's glibc is
  too new for the Deck). Deploy with `deck/deploy.sh` (refuses while the game runs).
- Measure, don't guess: compare builds by the Deck's `Render thread profile`
  "us per draw" and frames per 10 s, standing in the hub (docs: "Checking a build").
- On the Deck (`ssh deck@steamdeck`): uploading the build and reading logs or
  `/proc` is fine; deleting files, changing settings, or running/installing
  anything there needs the user's explicit permission. Touch nothing unrelated to reNut.
- Edit `~/.local/share/renut/renut.toml` only while the game is closed (it is
  rewritten on exit).
