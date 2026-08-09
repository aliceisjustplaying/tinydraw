#!/usr/bin/env node

import { execFileSync } from 'node:child_process'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import path from 'node:path'
import { fileURLToPath, pathToFileURL } from 'node:url'

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..')
const referenceRoot = path.join(root, 'reference/perfect-freehand')
const expectedCommit = '176e00f2399f4969e1b0965c5921d96a3e50ce9f'
const options = {
  size: 6,
  thinning: 0.55,
  smoothing: 0.55,
  streamline: 0.35,
  simulatePressure: true,
  start: { cap: true, taper: false },
  end: { cap: true, taper: false },
}

function verifyReference() {
  let actualCommit
  try {
    actualCommit = execFileSync('git', ['-C', referenceRoot, 'rev-parse', 'HEAD'], {
      encoding: 'utf8',
    }).trim()
  } catch {
    throw new Error('Clone perfect-freehand as documented in reference/PERFECT_FREEHAND.md')
  }
  if (actualCommit !== expectedCommit) {
    throw new Error(`perfect-freehand is at ${actualCommit}; expected ${expectedCommit}`)
  }
}

async function loadPerfectFreehand() {
  const modulePath = path.join(
    referenceRoot,
    'packages/perfect-freehand/dist/esm/index.mjs',
  )
  try {
    return await import(pathToFileURL(modulePath))
  } catch {
    throw new Error(
      'Build the reference first: cd reference/perfect-freehand && corepack yarn install --immutable && corepack yarn build:packages',
    )
  }
}

async function readStroke(name) {
  const source = path.join(root, 'testdata/strokes', `${name}.stroke`)
  const lines = (await readFile(source, 'utf8')).trim().split('\n')
  return lines.map((line, index) => {
    const [action, x, y, timestampUs, trailing] = line.trim().split(/\s+/)
    const parsed = { action, x: Number(x), y: Number(y), timestampUs: Number(timestampUs) }
    if (
      trailing ||
      !['down', 'move', 'up'].includes(action) ||
      !Number.isFinite(parsed.x) ||
      !Number.isFinite(parsed.y) ||
      !Number.isFinite(parsed.timestampUs)
    ) {
      throw new Error(`Invalid stroke input at ${source}:${index + 1}`)
    }
    return parsed
  })
}

function commonPointPrefix(left, right, epsilon = 1e-9) {
  const count = Math.min(left.length, right.length)
  let index = 0
  while (
    index < count &&
    Math.abs(left[index][0] - right[index][0]) <= epsilon &&
    Math.abs(left[index][1] - right[index][1]) <= epsilon
  ) {
    index += 1
  }
  return index
}

async function generateFixture(pf, name) {
  const input = await readStroke(name)
  const points = input.map(({ x, y }) => [x, y])
  const completeOptions = { ...options, last: true }
  const strokePoints = pf.getStrokePoints(points, completeOptions)
  const outline = pf.getStrokeOutlinePoints(strokePoints, completeOptions)
  const fixture = {
    source: {
      library: 'steveruizok/perfect-freehand',
      commit: expectedCommit,
      input: `testdata/strokes/${name}.stroke`,
    },
    options: completeOptions,
    input,
    strokePoints,
    outline,
  }
  const output = path.join(root, 'testdata/reference', `pf-${name}.json`)
  await mkdir(path.dirname(output), { recursive: true })
  await writeFile(output, `${JSON.stringify(fixture, null, 2)}\n`)
  const outlineOutput = path.join(root, 'testdata/reference', `pf-${name}.outline`)
  await writeFile(
    outlineOutput,
    `${outline.map(([x, y]) => `${x} ${y}`).join('\n')}\n`,
  )
  console.log(`wrote ${path.relative(root, output)}`)
  console.log(`wrote ${path.relative(root, outlineOutput)}`)
}

async function analyzeDependencies(pf, name) {
  const input = await readStroke(name)
  const observations = []
  let previousOutline = []
  for (let count = 1; count <= input.length; count += 1) {
    const points = input.slice(0, count).map(({ x, y }) => [x, y])
    const strokePoints = pf.getStrokePoints(points, { ...options, last: false })
    const outline = pf.getStrokeOutlinePoints(strokePoints, { ...options, last: false })
    const stablePrefix = commonPointPrefix(previousOutline, outline)
    observations.push({
      inputCount: count,
      strokePointCount: strokePoints.length,
      outlinePointCount: outline.length,
      stableOutlinePrefixFromPrevious: stablePrefix,
      previousOutlineTailChanged: previousOutline.length - stablePrefix,
    })
    previousOutline = outline
  }
  const report = {
    source: {
      library: 'steveruizok/perfect-freehand',
      commit: expectedCommit,
      input: `testdata/strokes/${name}.stroke`,
    },
    caveat:
      'Outline arrays concatenate left track, end cap, reversed right track, and start cap. Prefix stability measures only the stable beginning of the left track; it is evidence, not yet a commit-horizon proof.',
    options: { ...options, last: false },
    observations,
  }
  const output = path.join(root, 'testdata/reference', `pf-${name}-dependency.json`)
  await mkdir(path.dirname(output), { recursive: true })
  await writeFile(output, `${JSON.stringify(report, null, 2)}\n`)
  console.log(`wrote ${path.relative(root, output)}`)
}

verifyReference()
const pf = await loadPerfectFreehand()
await generateFixture(pf, 'zigzag')
await generateFixture(pf, 'dependency-probe')
await analyzeDependencies(pf, 'dependency-probe')
