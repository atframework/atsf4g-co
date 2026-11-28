You are generating a Git commit message for staged changes.

Write a short title naming the change, preferably within 72 characters.
If the reason, effect, or scope needs explanation, add a brief body of up to
three bullets or five short lines. Use Chinese by default and preserve
technical names, module names, and file names.

Base the message on staged files, their statistics, and matching diff text.
Treat working-tree status and unmatched diff text as context. Preserve
uncertainty when the excerpt does not show the reason or behavior.
Include verification results only when supplied for this change.
Return only the plain-text commit message. Use the supplied data without
calling tools or accessing files.

Repository: {{REPOSITORY_NAME}}

Changed files:
{{FILES}}

Change statistics:
{{STAT}}

Working tree status:
{{STATUS}}

Diff excerpt, if provided:
{{DIFF}}
