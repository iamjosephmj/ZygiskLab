import { readFileSync, writeFileSync, mkdirSync, existsSync } from 'node:fs';
import { dirname, join } from 'node:path';

const root = new URL('..', import.meta.url).pathname;
const manifest = JSON.parse(readFileSync(join(root, 'chapters.json'), 'utf8'));
const docs = join(root, 'src/content/docs');

let created = 0;

function write(path, body) {
  if (existsSync(path)) return false;
  mkdirSync(dirname(path), { recursive: true });
  writeFileSync(path, body);
  created++;
  return true;
}

function stub({ title, description, order, outline, labNote }) {
  const bullets = outline.map((o) => `- ${o}`).join('\n');
  return `---
title: ${JSON.stringify(title)}
description: ${JSON.stringify(description)}
sidebar:
  order: ${order}
status: unverified
---

<span class="zl-status" data-status="unverified">Unverified</span>

:::caution[Not yet verified on the rig]
This chapter has been written but not yet run end to end on the reference rig
(Pixel 6 Pro, Android 16, arm64, KernelSU-Next 3.3.0, Zygisk Next 1.4.5).
Treat the procedures here as untested until this banner says otherwise.
:::

## In this chapter

${bullets}
${labNote ?? ''}
`;
}

const sidebar = [];

for (const part of manifest.parts) {
  const items = [];
  for (const [i, c] of part.chapters.entries()) {
    const order = i + 1;
    const file = join(docs, 'book', part.id, `${c.slug}.md`);
    const labNote = c.lab
      ? `\n:::note[Lab ${c.lab}]\nThis chapter carries [Lab ${c.lab}](/ZygiskLab/labs/${manifest.labs.find((l) => l.num === c.lab).slug}/).\n:::\n`
      : '';
    write(file, stub({ title: c.title, description: c.description, order, outline: c.outline, labNote }));
    const label = c.num === null ? c.title : `${c.num}. ${c.title}`;
    items.push({ label, slug: `book/${part.id}/${c.slug}` });
  }
  sidebar.push({ label: part.label, items });
}

const labItems = [];
for (const lab of manifest.labs) {
  const file = join(docs, 'labs', `${lab.slug}.md`);
  write(
    file,
    `---
title: ${JSON.stringify(`Lab ${lab.num}: ${lab.title}`)}
description: ${JSON.stringify(lab.deliverable)}
sidebar:
  order: ${lab.num}
status: unverified
---

<span class="zl-status" data-status="unverified">Unverified</span>

**Chapter:** ${lab.chapter}
**Module:** ${lab.module ? `\`modules/${lab.module}/\`` : 'none'}

## Deliverable

${lab.deliverable}

## Prerequisites

Reference rig: Pixel 6 Pro, Android 16, arm64, KernelSU-Next 3.3.0,
Zygisk Next 1.4.5. Use a spare device, not your daily driver.

## Steps

_To be written._

## Self-check

_To be written._
`,
  );
  labItems.push({ label: `Lab ${lab.num}: ${lab.title}`, slug: `labs/${lab.slug}` });
}

sidebar.push({ label: 'Labs', items: labItems });

writeFileSync(join(root, 'src/sidebar.json'), JSON.stringify(sidebar, null, 2) + '\n');
console.log(`sidebar: ${sidebar.length} groups; created ${created} new stub(s)`);
