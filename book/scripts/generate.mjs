import { readFileSync, writeFileSync, mkdirSync, existsSync, readdirSync, statSync } from 'node:fs';
import { dirname, join, relative } from 'node:path';

const root = new URL('..', import.meta.url).pathname;
const manifest = JSON.parse(readFileSync(join(root, 'chapters.json'), 'utf8'));
const docs = join(root, 'src/content/docs');

const REFERENCE_RIG = 'Pixel 6 Pro, Android 16, arm64, KernelSU-Next 3.3.0, Zygisk Next 1.4.5';
const ROE_SLUG = 'book/foundations/02-rules-of-engagement';

const checkMode = process.argv.includes('--check');

if (checkMode) {
  runCheck();
} else {
  runGenerate();
}

// ---------------------------------------------------------------------------
// --check: report drift between the manifest and what's on disk. Never
// deletes or rewrites anything - it only reports, and exits non-zero so CI
// can fail on drift. Two failure modes:
//   (a) a .md page under content/docs/book/ or content/docs/labs/ that no
//       manifest entry claims (an orphan left behind by a slug rename or a
//       chapter removed from the manifest)
//   (b) a page whose frontmatter `title` no longer matches the manifest's
//       title for that slug (a title changed in the manifest but the file
//       on disk was never updated)
//   (c) an internal /ZygiskLab/... link pointing at a page that does not
//       exist. Starlight builds a dead internal link without complaint, so
//       nothing else in the pipeline catches these.
// ---------------------------------------------------------------------------
function runCheck() {
  const claimed = new Map(); // relative path (posix, no extension concerns) -> expected title

  for (const part of manifest.parts) {
    for (const c of part.chapters) {
      claimed.set(join('book', part.id, `${c.slug}.md`), c.title);
    }
  }
  for (const lab of manifest.labs) {
    claimed.set(join('labs', `${lab.slug}.md`), `Lab ${lab.num}: ${lab.title}`);
  }

  const onDisk = [...listMarkdown(join(docs, 'book')), ...listMarkdown(join(docs, 'labs'))].map((p) =>
    relative(docs, p),
  );

  const orphans = onDisk.filter((p) => !claimed.has(p));

  const titleMismatches = [];
  for (const [relPath, expectedTitle] of claimed) {
    const abs = join(docs, relPath);
    if (!existsSync(abs)) continue; // not-yet-generated stub; not a drift failure
    const actualTitle = readFrontmatterTitle(abs);
    if (actualTitle !== null && actualTitle !== expectedTitle) {
      titleMismatches.push({ relPath, expectedTitle, actualTitle });
    }
  }

  // (c) Dead internal links. Every page on disk is a valid target, keyed by
  // its slug path; a link is /ZygiskLab/<slug>/ with optional trailing slash
  // and anchor.
  const validTargets = new Set(onDisk.map((p) => p.replace(/\.mdx?$/, '')));
  for (const p of listMarkdown(docs)) {
    const rel = relative(docs, p);
    if (!validTargets.has(rel.replace(/\.mdx?$/, ''))) validTargets.add(rel.replace(/\.mdx?$/, ''));
  }

  const deadLinks = [];
  for (const abs of listMarkdown(docs)) {
    const rel = relative(docs, abs);
    const body = readFileSync(abs, 'utf8');
    for (const m of body.matchAll(/\]\((\/ZygiskLab\/[^)#\s]*)/g)) {
      const target = m[1].slice('/ZygiskLab/'.length).replace(/\/$/, '');
      if (target === '') continue; // the site root
      if (!validTargets.has(target)) deadLinks.push({ rel, target });
    }
  }

  if (orphans.length === 0 && titleMismatches.length === 0 && deadLinks.length === 0) {
    console.log('check: no drift between chapters.json and content/docs/, and no dead internal links');
    process.exit(0);
  }

  if (deadLinks.length) {
    console.error(`check: ${deadLinks.length} dead internal link(s):`);
    for (const { rel, target } of deadLinks) console.error(`  - ${rel} -> /ZygiskLab/${target}/`);
  }

  if (orphans.length) {
    console.error(`check: ${orphans.length} page(s) on disk with no manifest entry (orphaned by a slug rename or removal):`);
    for (const p of orphans) console.error(`  - ${p}`);
  }
  if (titleMismatches.length) {
    console.error(`check: ${titleMismatches.length} page(s) whose frontmatter title no longer matches the manifest:`);
    for (const { relPath, expectedTitle, actualTitle } of titleMismatches) {
      console.error(`  - ${relPath}: frontmatter has ${JSON.stringify(actualTitle)}, manifest says ${JSON.stringify(expectedTitle)}`);
    }
  }
  process.exit(1);
}

function listMarkdown(dir) {
  if (!existsSync(dir)) return [];
  const out = [];
  for (const entry of readdirSync(dir)) {
    const full = join(dir, entry);
    const st = statSync(full);
    if (st.isDirectory()) out.push(...listMarkdown(full));
    else if (entry.endsWith('.md')) out.push(full);
  }
  return out;
}

function readFrontmatterTitle(file) {
  const text = readFileSync(file, 'utf8');
  const match = text.match(/^---\n([\s\S]*?)\n---/);
  if (!match) return null;
  const titleLine = match[1].split('\n').find((l) => l.startsWith('title:'));
  if (!titleLine) return null;
  try {
    return JSON.parse(titleLine.slice('title:'.length).trim());
  } catch {
    return null;
  }
}

// ---------------------------------------------------------------------------
// Generation. Only ever creates - never overwrites an existing file.
// ---------------------------------------------------------------------------
function runGenerate() {
  let created = 0;

  function write(path, body) {
    if (existsSync(path)) return false;
    mkdirSync(dirname(path), { recursive: true });
    writeFileSync(path, body);
    created++;
    return true;
  }

  function stub({ title, description, order, outline, labNote, blurb, roeNote }) {
    const bullets = outline.map((o) => `- ${o}`).join('\n');
    return `---
title: ${JSON.stringify(title)}
description: ${JSON.stringify(description)}
sidebar:
  order: ${order}
status: unverified
---

${blurb}
${roeNote ?? ''}
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
      const roeNote =
        part.id === 'footprint'
          ? `\n:::caution[Detection and measurement]\nThis chapter covers detection mechanisms and measurement on systems you\nown or are authorised to assess. See [Rules of engagement](/ZygiskLab/${ROE_SLUG}/).\n:::\n`
          : undefined;
      write(
        file,
        stub({
          title: c.title,
          description: c.description,
          order,
          outline: c.outline,
          labNote,
          blurb: part.blurb,
          roeNote,
        }),
      );
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

**Chapter:** ${lab.chapter}
**Module:** ${lab.module ? `\`modules/${lab.module}/\`` : 'none'}

## Deliverable

${lab.deliverable}

## Prerequisites

Reference rig: ${REFERENCE_RIG}. Use a spare device, not your daily driver.

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
}
