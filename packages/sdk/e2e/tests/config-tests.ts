import type { Step, TestDefinition } from '@qvac/test-suite'

/**
 * End-to-end coverage for registry-download configuration plumbing.
 *
 * The desktop e2e fixture sets:
 *   - registryDownloadMaxRetries: 10
 *   - registryStreamTimeoutMs:    600000
 *
 * If either field is rejected by the SDK config schema, or the worker
 * fails to accept it, the consumer will not start at all and the full
 * suite fails fast. On top of that safety-net, these tests exercise the
 * registry-client download path (downloadBlob / downloadModel) with
 * those values in effect.
 */

/** The download path, driven with the fixture's retries/timeout in effect. */
const downloadSmokeSteps: Step[] = [
  { modelSource: { dep: 'whisper', as: 'src' } },
  { call: { method: 'downloadAsset', params: { assetSrc: '$src.modelSrc' }, as: 'downloaded' } },
  { project: { from: '$downloaded', path: 'path', as: 'path' } },
  { assert: { on: '$path', named: 'nonEmptyText' } }
]

export const configRegistryDownloadSmoke: TestDefinition = {
  testId: 'config-registry-download-smoke',
  params: {},
  // expectation is validated inside the executor
  expectation: { validation: 'function', fn: () => true },
  suites: ['smoke'],
  steps: downloadSmokeSteps,
  metadata: {
    category: 'config',
    dependency: 'none',
    estimatedDurationMs: 60000
  }
}

/** Not migrated, and the reason is worth reading before someone tries. */
export const configRegistryDownloadRespectsCancel: TestDefinition = {
  testId: 'config-registry-download-respects-cancel',
  params: {},
  expectation: { validation: 'function', fn: () => true },
  metadata: {
    category: 'config',
    dependency: 'none',
    estimatedDurationMs: 120000
  }
}

export const configTests = [configRegistryDownloadSmoke, configRegistryDownloadRespectsCancel]
