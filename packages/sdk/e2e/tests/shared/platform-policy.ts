/** Per-platform client policy, as data. */
export interface PlatformPolicy {
  /**
   * Which bootstrap download plan to use. Mobile fetches through the app's own storage and cannot
   * use the desktop concurrency.
   */
  downloadTarget: 'desktop' | 'mobile'

  /**
   * Milliseconds to sleep after a successful `unloadModel()` before the next load starts allocating
   * on top.
   */
  unloadSettleMs: number
}

export const PLATFORM_POLICY: Record<string, PlatformPolicy> = {
  desktop: { downloadTarget: 'desktop', unloadSettleMs: 0 },
  electron: { downloadTarget: 'desktop', unloadSettleMs: 0 },
  snap: { downloadTarget: 'desktop', unloadSettleMs: 0 },
  mobile: { downloadTarget: 'mobile', unloadSettleMs: 200 },
  python: { downloadTarget: 'desktop', unloadSettleMs: 0 }
}

/** The policy for a leg, by the first segment of its platform label. */
export function policyFor(platform: string): PlatformPolicy {
  const policy = PLATFORM_POLICY[platform.split('-')[0] ?? '']
  if (!policy) throw new Error(`no platform policy for "${platform}"`)
  return policy
}
