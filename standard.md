# Enterprise AI Infrastructure Engineer Journey Writing Standard

This document defines the writing and formatting standards for every chapter in this repository.

The objective is to produce a curriculum with the consistency and quality of a professionally published technical textbook.

---

# Core Principles

Every chapter must be:

- Accurate
- Beginner-friendly
- Enterprise-focused
- Vendor-neutral whenever possible
- Practical
- Concept-driven
- Consistent

The curriculum teaches engineering principles rather than memorization.

---

# Teaching Philosophy

Always teach concepts in this order:

1. What is it?
2. Why does it exist?
3. What problem does it solve?
4. How does it work internally?
5. How is it implemented?
6. How is it used in enterprise environments?
7. Common mistakes
8. Hands-on practice

Never begin with commands.

Commands are implementation details.

Understanding comes first.

---

# Chapter Template

Every chapter must follow this structure.

```
# Chapter Title

Learning Objectives

Prerequisites

Why This Matters

Core Concepts

Deep Dive

Enterprise Perspective

Real-world Example

Architecture Diagram

Hands-on Lab

Common Mistakes

Interview Questions

Summary

Further Reading

Next Chapter
```

---

# Writing Style

Use:

✔ Clear explanations

✔ Short paragraphs

✔ Professional tone

✔ Active voice

✔ Real-world analogies

✔ Enterprise examples

Avoid:

✘ Excessive jargon

✘ Unexplained acronyms

✘ Walls of text

✘ Tool-first teaching

✘ Vendor bias

---

# Code Standards

Every code block must:

- specify language
- include comments where helpful
- be tested
- use consistent formatting

Example:

```bash
sudo systemctl status ssh
```

---

# Command Formatting

Commands use:

```bash
```

Configuration files use:

```yaml
```

JSON uses:

```json
```

Python uses:

```python
```

Never use plain code blocks.

---

# Diagrams

Every complex topic should include a diagram.

Preferred formats:

- Mermaid
- SVG
- Draw.io
- Excalidraw

Diagrams should explain concepts rather than decorate pages.

---

# Hands-on Labs

Every major chapter should include:

Objective

Prerequisites

Environment

Steps

Expected Result

Troubleshooting

Challenge Exercise

Cleanup

---

# Enterprise Focus

Whenever possible answer:

How is this used in production?

How does this scale?

What are common failures?

How do enterprises solve this?

---

# Examples

Every chapter should contain at least:

- one analogy
- one production example
- one troubleshooting example

---

# Interview Preparation

Every chapter should end with interview questions.

Include:

Conceptual

Practical

Scenario-based

Architecture

Troubleshooting

---

# References

Whenever external information is used, reference official documentation first.

Preferred sources:

Linux Kernel Documentation

CNCF

Kubernetes Documentation

OpenTelemetry

SPIFFE

RFCs

Academic Papers

Vendor Documentation (when appropriate)

Avoid blogs unless no authoritative reference exists.

---

# Naming Convention

Folders:

```
01-Linux-Systems-Engineering
```

Files:

```
01-Introduction.md
```

Images:

```
linux-process-lifecycle.svg
```

Labs:

```
lab-01-process-management.md
```

---

# Markdown Style

Use:

- ATX headings (`#`)
- fenced code blocks
- relative links
- descriptive alt text

Avoid HTML unless absolutely necessary.

---

# Quality Checklist

Before merging a chapter verify:

✓ Technically accurate

✓ Grammar checked

✓ Numbering correct

✓ Links working

✓ Diagram included (if needed)

✓ Hands-on lab included

✓ Interview questions included

✓ Summary included

✓ Further reading included

---

# Repository Goal

This repository is intended to become one of the most comprehensive open-source curricula for Enterprise AI Infrastructure Engineering.

Every contribution should move it closer to that goal.

Quality takes precedence over speed.
