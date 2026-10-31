import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import { resolve } from 'node:path'
import { Resvg } from '@resvg/resvg-js'
import { PNG } from 'pngjs'

const root = resolve(import.meta.dirname, '../..')
const header = readFileSync(resolve(root, 'src/generated/icons.h'), 'utf8')
const names = ['thermometer', 'drop', 'three-drops', 'warning', 'thermometer-off', 'drop-off', 'three-drops-off']
const render = (svg, size) => PNG.sync.read(new Resvg(svg, { fitTo: { mode: 'width', value: size } }).render().asPng()).data

for (const name of names) {
  const svg = readFileSync(resolve(root, 'assets/icons', `${name}.svg`), 'utf8')
  for (const size of [32, 48]) {
    const symbol = `${name.replaceAll('-', '_')}_${size}`
    const definition = header.match(new RegExp(`inline constexpr uint8_t ${symbol}\\[${size * size}\\] = \\{([\\s\\S]*?)\\};`))
    assert.ok(definition, `Missing ${symbol}`)
    const actual = [...definition[1].matchAll(/0x([0-9a-f]{2})/g)].map(match => Number.parseInt(match[1], 16))
    const rgba = render(svg, size)
    const expected = []
    for (let offset = 3; offset < rgba.length; offset += 4) expected.push(rgba[offset])
    assert.deepEqual(actual, expected, `Incorrect alpha mask: ${symbol}`)
    assert.ok(actual.some(value => value > 0 && value < 255), `Missing antialiasing: ${symbol}`)
    if (name.endsWith('-off')) {
      const unmasked = svg.replace(/\s+mask="url\(#[^"]+\)"/, '')
      assert.notEqual(unmasked, svg, `Missing SVG mask on ${name}`)
      const withoutGap = render(unmasked, size)
      assert.ok(expected.some((value, index) => value < withoutGap[index * 4 + 3]), `Mask gap was not rendered: ${symbol}`)
    }
  }
}
const logoSvg = readFileSync(resolve(root, 'assets/logo-aquamax.svg'), 'utf8')
const logoDefinition = header.match(/inline constexpr uint8_t logo_176_43\[176 \* 43\] = \{([\s\S]*?)\};/)
assert.ok(logoDefinition, 'Missing AquaMax logo')
const logoActual = [...logoDefinition[1].matchAll(/0x([0-9a-f]{2})/g)].map(match => Number.parseInt(match[1], 16))
const logoRgba = render(logoSvg, 176)
const logoExpected = []
for (let offset = 3; offset < logoRgba.length; offset += 4) logoExpected.push(logoRgba[offset])
assert.deepEqual(logoActual, logoExpected, 'Incorrect AquaMax logo mask')
assert.ok(logoActual.some(value => value > 0 && value < 255), 'Missing logo antialiasing')
console.log('All generated icon and logo masks match their SVGs; masked gaps and antialiasing are present.')