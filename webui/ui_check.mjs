// Headless check: load the built UI, verify the LCD effect rack renders, no
// horizontal overflow at 1024 & 1340, and selecting a slot type spawns its knobs.
import { chromium } from 'playwright'
import { dirname, join } from 'node:path'
import { fileURLToPath, pathToFileURL } from 'node:url'

const here = dirname(fileURLToPath(import.meta.url))
const indexUrl = pathToFileURL(join(here, '../plugin/resources/web/index.html')).href

const browser = await chromium.launch()
const errors = []

for (const [w, h] of [
  [1024, 740],
  [1340, 900],
]) {
  const page = await browser.newPage({ viewport: { width: w, height: h } })
  page.on('pageerror', (e) => errors.push(`PAGEERROR: ${e.message}`))
  page.on('console', (m) => {
    if (m.type() === 'error') errors.push(m.text())
  })
  await page.goto(indexUrl, { waitUntil: 'networkidle' })
  await page.waitForTimeout(400)
  const m = await page.evaluate(() => ({
    overflow: document.documentElement.scrollWidth > document.documentElement.clientWidth,
    hasChain: document.body.innerText.includes('EFFECT CHAIN'),
    hasLock: document.body.innerText.includes('EFFECT LOCK'),
    slotTriggers: document.querySelectorAll('[data-testid^="fx-type-trigger-"]').length,
    knobs: document.querySelectorAll('svg[aria-label="knob"]').length,
  }))
  console.log(
    `[${w}x${h}] overflowX=${m.overflow} EFFECT_CHAIN=${m.hasChain} EFFECT_LOCK=${m.hasLock} slotTriggers=${m.slotTriggers} knobs=${m.knobs}`,
  )
  await page.screenshot({ path: join(here, `ui_fx_${w}.png`), fullPage: true })
  await page.close()
}

// interaction: open slot-0 type menu, pick Reverb, confirm its knobs appear.
{
  const page = await browser.newPage({ viewport: { width: 1340, height: 900 } })
  page.on('pageerror', (e) => errors.push(`PAGEERROR: ${e.message}`))
  await page.goto(indexUrl, { waitUntil: 'networkidle' })
  await page.waitForTimeout(300)
  const knobsBefore = await page.locator('svg[aria-label="knob"]').count()
  await page.locator('[data-testid="fx-type-trigger-0"]').click()
  await page.waitForTimeout(150)
  await page.getByRole('menuitem', { name: 'Reverb', exact: true }).click()
  await page.waitForTimeout(250)
  const knobsAfter = await page.locator('svg[aria-label="knob"]').count()
  const hasMix = (await page.locator('body').innerText()).includes('MIX')
  console.log(`[interact] knobsBefore=${knobsBefore} knobsAfter=${knobsAfter} reverbMixKnob=${hasMix}`)
  await page.screenshot({ path: join(here, 'ui_fx.png'), fullPage: true })
  await page.close()
}

console.log('console/page errors:', errors.length)
for (const e of errors.slice(0, 10)) console.log('  •', e)
await browser.close()
