# Issue tracker: GitHub

Issues and specs live as GitHub issues. Use the `gh` CLI.

- Create: `gh issue create --title "..." --body "..."`
- Read: `gh issue view <number> --comments`
- List: `gh issue list --state open --json number,title,body,labels,comments`
- Comment: `gh issue comment <number> --body "..."`
- Label: `gh issue edit <number> --add-label "..."` or `--remove-label "..."`
- Close: `gh issue close <number> --comment "..."`

Infer the repository from the current Git remote.

## Pull requests as a triage surface

**PRs as a request surface: no.**

## Release tracking

- Close an issue when its implementation PR merges into `dev`.
- Assign the issue a GitHub Milestone for its intended release; milestones distinguish merged-but-unreleased work from released work.
- Use the five triage labels only for the state of open work. Do not use them as release-status labels.
- Close a release milestone after its release PR merges into `main` and the version tag is pushed.

## Skill vocabulary

“Publish to the issue tracker” means create a GitHub issue. “Fetch the relevant ticket” means run `gh issue view <number> --comments`.

## Wayfinding

A map is one issue labelled `wayfinder:map`; child tickets are linked GitHub sub-issues, or a task list fallback. Use native GitHub issue dependencies for blocking. Claim a ticket with `gh issue edit <n> --add-assignee @me`; resolve it with a comment, close it, then link the decision from the map.
