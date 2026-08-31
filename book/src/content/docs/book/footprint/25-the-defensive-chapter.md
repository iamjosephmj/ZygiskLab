---
title: "The defensive chapter"
description: "What app-side detection is worth doing, what is theatre, and why defence in depth beats blocking on the first hit."
sidebar:
  order: 5
status: unverified
---

The traces you left, stage by stage — and how apps look for them.

:::caution[Detection and measurement]
This chapter covers detection mechanisms and measurement on systems you
own or are authorised to assess. See [Rules of engagement](/ZygiskLab/book/foundations/02-rules-of-engagement/).
:::

## In this chapter

- Written for the app author, not the module author
- What is worth checking, ordered by cost-to-benefit
- What is theatre: checks that are trivially defeated, expensive, or that fire on legitimate users
- Defence in depth: why no single check is the answer, and where server-side signals belong
- Failing well: what to do when a check fires, and why blocking immediately is usually the wrong response
- What genuinely raises the cost for an attacker, honestly assessed

