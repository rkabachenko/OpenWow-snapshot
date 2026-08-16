# Contributing to OpenWoW

Thanks for your interest. A few things are worth knowing before you start.

## Ground rules

**Behaviour is measured against the original client.** The goal is that
software written for the original 3.3.5a client cannot tell the difference. If
something built for 3.3.5a does not work here, this client is wrong — the fix
belongs in this code, not in the thing that was working.

**Changes need evidence, not plausibility.** A patch that makes a symptom go
away without establishing *why* the behaviour differed is not usually
mergeable. Say what the correct behaviour is, and how you know.

**Keep the scope tight.** One change per pull request, with a description that
explains the reasoning. Match the conventions of the code around you.

## What is most useful

- Reproducible bug reports, especially with a screenshot or clip showing how
  the original client behaves in the same situation.
- Build and platform fixes.
- Compatibility reports from addons that misbehave.

## Contributor licence terms

**Please read this before submitting a pull request.**

By submitting a contribution, you agree to assign copyright in that
contribution to the project maintainer, who may license it both under the AGPL
and under separate commercial terms.

This is not a formality and it is not about ownership for its own sake. The
project is offered under the AGPL *and* under commercial terms for users who
cannot meet the AGPL's conditions. That dual arrangement only remains possible
if a single party holds the rights to the whole codebase. If contributions
arrived under the AGPL alone, that option would disappear permanently for
everyone.

You retain the right to use your own contribution however you wish. You are
confirming that the work is yours to give: that you wrote it, that it is not
derived from code you do not have the right to relicense, and that no employer
or other party has a claim to it.

If you cannot agree to these terms, please open an issue describing the problem
and the fix rather than sending a pull request — a good bug report is a real
contribution in its own right.

## Reverse engineering

Do not submit code, comments, or documentation copied or transcribed from
disassembly or decompilation of the original client, and do not attach such
material to issues. Describe observed behaviour instead: what the original does,
under what conditions, and how it differs from what this client does.
