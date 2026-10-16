# Agent notes: Inventatory-Firmware

Shared registry for everyone working on this repository, including cloud sessions. Read it before you edit anything.
The rules are in AGENTS.md section 15 (on main).

Files:
- REGISTRY.md: one line per active task. Check it first.
- <task-slug>.md: one note per task, from TEMPLATE.md.

Read the latest copy:

    git fetch origin agent-notes
    git show origin/agent-notes:REGISTRY.md

Update it through a short-lived worktree, never in another task's checkout:

    git fetch origin agent-notes
    git worktree add --detach ../notes-<slug> origin/agent-notes
    # edit the files in ../notes-<slug>, then:
    git -C ../notes-<slug> commit -am "docs(project): <what changed>"
    git push origin HEAD:agent-notes
    git worktree remove ../notes-<slug>

A push that is not a fast-forward means someone else updated the registry first. Fetch, merge, and check the registry again.
