---
name: task
description: Work on a task from TASKS.md by number
disable-model-invocation: true
user-invocable: true
allowed-tools: Read, Grep, Glob, Bash, Edit, Write, AskUserQuestion
argument-hint: [task-number]
---

# Work on a task from TASKS.md

## Setup

1. If `$ARGUMENTS` is empty or blank, read `TASKS.md` and list all unchecked (`[ ]`) tasks with their numbers, then use `AskUserQuestion` to ask the user which task number to work on. Use that answer as the task number going forward.
2. Read `TASKS.md` and find the task matching the chosen task number.
3. Verify the task is not already checked off (`[x]`). If it is, tell the user and stop.
4. Read the current git branch. If you are not on `main`, ask the user before proceeding.

In the steps below, **TASK_NUMBER** refers to `$ARGUMENTS` if provided, or the number chosen by the user in step 1.

## Workflow

Follow the instructions at the top of `TASKS.md`. Specifically:

### 1. Create a GitHub issue

- Use `gh issue create` with a title matching the task description and a body that expands on what needs to be done.
- Note the issue number returned.

### 2. Create a feature branch and initial commit

- Branch from `main` with a descriptive name (e.g., `debounce-youtube-fetch` for task 2).
- Make an initial empty commit or a small scaffolding commit that references the issue: `Link task TASK_NUMBER to GitHub issue #<issue-number>`.

### 3. Implement the task

- Plan first, then implement. Run `npm run build` and `npm run lint` to verify your changes compile and lint cleanly.
- Make incremental commits as you go.

### 4. Mark the task as done

- Edit `TASKS.md`: change `[ ]` to `[x]` for task **#TASK_NUMBER**.
- Commit this change to the branch with the message: `Mark task TASK_NUMBER as completed in TASKS.md`.

### 5. Create a pull request

- Push the branch and open a PR with `gh pr create`, linking the issue in the body (use `Closes #<issue-number>`).
- The PR title should be concise and the body should summarize the changes.
- The PR must include the TASKS.md check-off commit alongside the implementation commits.

### 6. Report back

- Print the PR URL and issue URL so the user can review.
