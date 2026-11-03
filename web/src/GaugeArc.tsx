const START = 135;      // bottom-left, matches your path's start point
const SWEEP = 270;
const SEGMENTS = 90;
const R = 92;

const temperatureStops = [
  { t: 0,    c: [4, 98, 221] },
  { t: 0.48, c: [1, 226, 143] },
  { t: 0.75, c: [252, 104, 25] },
  { t: 1,    c: [229, 9, 52] },
];

function colorAt(t: number) {
  for (let i = 0; i < temperatureStops.length - 1; i++) {
    const a = temperatureStops[i], b = temperatureStops[i + 1];
    if (t >= a.t && t <= b.t) {
      const k = (t - a.t) / (b.t - a.t);
      const rgb = a.c.map((v, j) => Math.round(v + (b.c[j] - v) * k));
      return `rgb(${rgb.join(",")})`;
    }
  }
  return `rgb(${temperatureStops[temperatureStops.length - 1].c.join(",")})`;
}

function point(deg: number) {
  const r = (deg * Math.PI) / 180;
  return [100 + R * Math.cos(r), 100 + R * Math.sin(r)];
}

// Computed once at module load, since colors and geometry are fixed
export const SEGS = Array.from({ length: SEGMENTS }, (_, i) => {
  const t0 = i / SEGMENTS, t1 = (i + 1) / SEGMENTS;
  const [x0, y0] = point(START + SWEEP * t0);
  const [x1, y1] = point(START + SWEEP * t1);
  return {
    d: `M ${x0} ${y0} A ${R} ${R} 0 0 1 ${x1} ${y1}`,
    color: colorAt((t0 + t1) / 2),
  };
});