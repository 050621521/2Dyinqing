import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { deflateSync } from "node:zlib";

const outDir = path.dirname(fileURLToPath(import.meta.url));
const frameDir = path.join(outDir, "frames_mechanic");
const spriteSheetPath = path.join(outDir, "43_002_机械师_移动_精灵表.png");
const previewPath = path.join(outDir, "43_002_机械师_移动_预览.html");
const metadataPath = path.join(outDir, "43_002_机械师_移动_元数据.json");

const frameSize = 64;
const frameCount = 6;
const directions = [
  { key: "down", label: "下", row: 0 },
  { key: "down_right", label: "右下", row: 1 },
  { key: "right", label: "右", row: 2 },
  { key: "up_right", label: "右上", row: 3 },
  { key: "up", label: "上", row: 4 },
  { key: "up_left", label: "左上", row: 5 },
  { key: "left", label: "左", row: 6 },
  { key: "down_left", label: "左下", row: 7 },
];

const palette = {
  outline: [15, 18, 24, 255],
  shadow: [0, 0, 0, 72],
  skin: [224, 169, 116, 255],
  skinDark: [172, 104, 70, 255],
  hair: [74, 52, 39, 255],
  cap: [54, 104, 145, 255],
  capDark: [34, 66, 94, 255],
  capLight: [96, 154, 197, 255],
  suit: [45, 111, 156, 255],
  suitDark: [28, 70, 104, 255],
  suitLight: [89, 157, 201, 255],
  glove: [40, 42, 47, 255],
  belt: [91, 71, 52, 255],
  brass: [210, 150, 58, 255],
  metal: [164, 179, 185, 255],
  glass: [115, 206, 221, 255],
  glassDark: [47, 94, 117, 255],
  boot: [24, 28, 35, 255],
  pants: [37, 58, 78, 255],
  eye: [24, 22, 20, 255],
  backpack: [69, 82, 92, 255],
  backpackLight: [114, 132, 141, 255],
};

const crcTable = (() => {
  const table = new Uint32Array(256);
  for (let n = 0; n < 256; n += 1) {
    let c = n;
    for (let k = 0; k < 8; k += 1) {
      c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
    }
    table[n] = c >>> 0;
  }
  return table;
})();

function crc32(buffer) {
  let crc = 0xffffffff;
  for (const byte of buffer) {
    crc = crcTable[(crc ^ byte) & 0xff] ^ (crc >>> 8);
  }
  return (crc ^ 0xffffffff) >>> 0;
}

function pngChunk(type, data) {
  const name = Buffer.from(type);
  const length = Buffer.alloc(4);
  const crc = Buffer.alloc(4);
  length.writeUInt32BE(data.length, 0);
  crc.writeUInt32BE(crc32(Buffer.concat([name, data])), 0);
  return Buffer.concat([length, name, data, crc]);
}

function encodePng(width, height, rgba) {
  const header = Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]);
  const ihdr = Buffer.alloc(13);
  ihdr.writeUInt32BE(width, 0);
  ihdr.writeUInt32BE(height, 4);
  ihdr[8] = 8;
  ihdr[9] = 6;
  ihdr[10] = 0;
  ihdr[11] = 0;
  ihdr[12] = 0;

  const rowBytes = width * 4;
  const raw = Buffer.alloc((rowBytes + 1) * height);
  for (let y = 0; y < height; y += 1) {
    const rawOffset = y * (rowBytes + 1);
    raw[rawOffset] = 0;
    Buffer.from(rgba.buffer, rgba.byteOffset + y * rowBytes, rowBytes).copy(raw, rawOffset + 1);
  }

  return Buffer.concat([
    header,
    pngChunk("IHDR", ihdr),
    pngChunk("IDAT", deflateSync(raw, { level: 9 })),
    pngChunk("IEND", Buffer.alloc(0)),
  ]);
}

class PixelCanvas {
  constructor(width, height) {
    this.width = width;
    this.height = height;
    this.data = new Uint8ClampedArray(width * height * 4);
  }

  blendPixel(x, y, color) {
    x = Math.round(x);
    y = Math.round(y);
    if (x < 0 || y < 0 || x >= this.width || y >= this.height) return;
    const offset = (y * this.width + x) * 4;
    const sourceAlpha = color[3] / 255;
    const destAlpha = this.data[offset + 3] / 255;
    const outAlpha = sourceAlpha + destAlpha * (1 - sourceAlpha);
    if (outAlpha <= 0) return;

    for (let channel = 0; channel < 3; channel += 1) {
      this.data[offset + channel] = Math.round(
        (color[channel] * sourceAlpha + this.data[offset + channel] * destAlpha * (1 - sourceAlpha)) / outAlpha,
      );
    }
    this.data[offset + 3] = Math.round(outAlpha * 255);
  }

  fillRect(x, y, width, height, color) {
    const left = Math.round(x);
    const top = Math.round(y);
    const right = Math.round(x + width);
    const bottom = Math.round(y + height);
    for (let yy = top; yy < bottom; yy += 1) {
      for (let xx = left; xx < right; xx += 1) this.blendPixel(xx, yy, color);
    }
  }

  fillEllipse(cx, cy, rx, ry, color) {
    const left = Math.floor(cx - rx);
    const right = Math.ceil(cx + rx);
    const top = Math.floor(cy - ry);
    const bottom = Math.ceil(cy + ry);
    for (let y = top; y <= bottom; y += 1) {
      for (let x = left; x <= right; x += 1) {
        const nx = (x + 0.5 - cx) / rx;
        const ny = (y + 0.5 - cy) / ry;
        if (nx * nx + ny * ny <= 1) this.blendPixel(x, y, color);
      }
    }
  }

  copyFrom(source, dx, dy) {
    for (let y = 0; y < source.height; y += 1) {
      for (let x = 0; x < source.width; x += 1) {
        const offset = (y * source.width + x) * 4;
        const alpha = source.data[offset + 3];
        if (!alpha) continue;
        this.blendPixel(dx + x, dy + y, [
          source.data[offset],
          source.data[offset + 1],
          source.data[offset + 2],
          alpha,
        ]);
      }
    }
  }
}

function savePng(filePath, canvas) {
  fs.writeFileSync(filePath, encodePng(canvas.width, canvas.height, canvas.data));
}

function drawOutlinedEllipse(canvas, cx, cy, rx, ry, fill, outline = palette.outline, outlineSize = 2) {
  canvas.fillEllipse(cx, cy, rx + outlineSize, ry + outlineSize, outline);
  canvas.fillEllipse(cx, cy, rx, ry, fill);
}

function drawOutlinedRect(canvas, x, y, width, height, fill, outline = palette.outline, outlineSize = 2) {
  canvas.fillRect(x - outlineSize, y - outlineSize, width + outlineSize * 2, height + outlineSize * 2, outline);
  canvas.fillRect(x, y, width, height, fill);
}

function drawLeg(canvas, x, y, height = 8) {
  drawOutlinedRect(canvas, x - 3, y - height, 6, height, palette.pants, palette.outline, 1);
}

function drawBoot(canvas, x, y, mirror = 1) {
  drawOutlinedRect(canvas, x - 3, y - 1, 7, 5, palette.boot, palette.outline, 1);
  canvas.fillRect(x + mirror * 1, y + 3, 5, 2, [47, 55, 65, 255]);
}

function drawToolFist(canvas, x, y, tool = false) {
  drawOutlinedEllipse(canvas, x, y, 4, 4, palette.glove, palette.outline, 1);
  canvas.fillRect(x - 2, y - 1, 4, 2, [71, 75, 82, 255]);
  if (tool) {
    canvas.fillRect(x + 3, y - 7, 2, 11, palette.metal);
    canvas.fillRect(x + 1, y - 8, 6, 2, palette.brass);
  }
}

function drawGogglesFront(canvas, cx, cy) {
  drawOutlinedRect(canvas, cx - 10, cy - 3, 8, 6, palette.glass, palette.outline, 1);
  drawOutlinedRect(canvas, cx + 2, cy - 3, 8, 6, palette.glass, palette.outline, 1);
  canvas.fillRect(cx - 2, cy - 1, 4, 2, palette.outline);
  canvas.fillRect(cx - 8, cy - 2, 3, 2, [193, 237, 240, 255]);
  canvas.fillRect(cx + 4, cy - 2, 3, 2, [193, 237, 240, 255]);
}

function drawGogglesSide(canvas, cx, cy, sign) {
  drawOutlinedRect(canvas, cx + sign * 1, cy - 3, 9 * sign, 6, palette.glass, palette.outline, 1);
  canvas.fillRect(cx + sign * 3, cy - 2, 3 * sign, 2, [193, 237, 240, 255]);
  canvas.fillRect(cx - sign * 7, cy - 1, 8 * sign, 2, palette.glassDark);
}

function drawCapFront(canvas, cx, cy) {
  canvas.fillEllipse(cx, cy - 7, 12, 6, palette.cap);
  canvas.fillRect(cx - 10, cy - 12, 20, 7, palette.cap);
  canvas.fillRect(cx - 6, cy - 13, 12, 3, palette.capLight);
  canvas.fillRect(cx - 14, cy - 6, 28, 4, palette.capDark);
}

function drawCapBack(canvas, cx, cy) {
  canvas.fillEllipse(cx, cy - 6, 13, 7, palette.capDark);
  canvas.fillRect(cx - 10, cy - 12, 20, 9, palette.cap);
  canvas.fillRect(cx - 5, cy - 13, 10, 3, palette.capLight);
}

function drawCapSide(canvas, cx, cy, sign) {
  canvas.fillEllipse(cx - sign * 1, cy - 7, 12, 6, palette.cap);
  canvas.fillRect(cx - 9, cy - 12, 18, 8, palette.cap);
  canvas.fillRect(cx + sign * 4, cy - 6, 12 * sign, 3, palette.capDark);
  canvas.fillRect(cx - 3, cy - 13, 8, 3, palette.capLight);
}

function drawBody(canvas, cx, y, bob, facing) {
  const bodyY = y + bob;
  if (facing === "up") {
    drawOutlinedRect(canvas, cx - 9, bodyY, 18, 19, palette.backpack, palette.outline, 2);
    canvas.fillRect(cx - 6, bodyY + 2, 12, 3, palette.backpackLight);
    canvas.fillRect(cx - 8, bodyY + 9, 16, 4, palette.suitDark);
  } else {
    drawOutlinedRect(canvas, cx - 10, bodyY, 20, 19, palette.suit, palette.outline, 2);
    canvas.fillRect(cx - 8, bodyY + 2, 16, 3, palette.suitLight);
    canvas.fillRect(cx - 10, bodyY + 12, 20, 4, palette.suitDark);
    canvas.fillRect(cx - 10, bodyY + 15, 20, 3, palette.belt);
    canvas.fillRect(cx + 2, bodyY + 15, 5, 3, palette.brass);
  }

  if (facing === "side") {
    canvas.fillRect(cx + 8, bodyY + 3, 5, 13, palette.backpack);
    canvas.fillRect(cx + 9, bodyY + 5, 2, 6, palette.backpackLight);
  }
}

function drawFront(canvas, frame) {
  const cx = 32;
  const t = (Math.PI * 2 * frame) / frameCount;
  const stride = Math.round(Math.sin(t) * 3);
  const bob = [0, -1, -2, -1, 0, -1][frame];
  const arm = Math.round(Math.sin(t + Math.PI) * 2);

  canvas.fillEllipse(cx, 55, 16, 5, palette.shadow);
  drawLeg(canvas, cx - 6, 45 + stride, 8);
  drawBoot(canvas, cx - 6, 48 + stride, -1);
  drawLeg(canvas, cx + 6, 45 - stride, 8);
  drawBoot(canvas, cx + 6, 48 - stride, 1);

  drawOutlinedRect(canvas, cx - 18, 30 + bob + arm, 7, 15, palette.suitDark, palette.outline, 1);
  drawOutlinedRect(canvas, cx + 11, 30 + bob - arm, 7, 15, palette.suitDark, palette.outline, 1);
  drawToolFist(canvas, cx - 17, 44 + bob + arm, frame % 3 === 1);
  drawToolFist(canvas, cx + 17, 44 + bob - arm, frame % 3 === 4);

  drawBody(canvas, cx, 26, bob, "down");
  drawOutlinedEllipse(canvas, cx, 19 + bob, 12, 11, palette.skin, palette.outline, 2);
  drawCapFront(canvas, cx, 19 + bob);
  drawGogglesFront(canvas, cx, 20 + bob);
  canvas.fillRect(cx - 3, 27 + bob, 6, 1, palette.skinDark);
}

function drawBack(canvas, frame) {
  const cx = 32;
  const t = (Math.PI * 2 * frame) / frameCount;
  const stride = Math.round(Math.sin(t) * 3);
  const bob = [0, -1, -2, -1, 0, -1][frame];
  const arm = Math.round(Math.sin(t + Math.PI) * 2);

  canvas.fillEllipse(cx, 55, 16, 5, palette.shadow);
  drawLeg(canvas, cx - 6, 45 - stride, 8);
  drawBoot(canvas, cx - 6, 48 - stride, -1);
  drawLeg(canvas, cx + 6, 45 + stride, 8);
  drawBoot(canvas, cx + 6, 48 + stride, 1);

  drawOutlinedRect(canvas, cx - 18, 30 + bob - arm, 7, 15, palette.suitDark, palette.outline, 1);
  drawOutlinedRect(canvas, cx + 11, 30 + bob + arm, 7, 15, palette.suitDark, palette.outline, 1);
  drawToolFist(canvas, cx - 17, 44 + bob - arm, frame % 3 === 2);
  drawToolFist(canvas, cx + 17, 44 + bob + arm, frame % 3 === 5);

  drawBody(canvas, cx, 26, bob, "up");
  drawOutlinedEllipse(canvas, cx, 19 + bob, 12, 11, palette.hair, palette.outline, 2);
  drawCapBack(canvas, cx, 19 + bob);
  canvas.fillRect(cx - 9, 21 + bob, 18, 5, palette.hair);
}

function drawSide(canvas, frame, direction) {
  const sign = direction === "right" ? 1 : -1;
  const cx = 32;
  const t = (Math.PI * 2 * frame) / frameCount;
  const stride = Math.round(Math.sin(t) * 3);
  const bob = [0, -1, -2, -1, 0, -1][frame];
  const arm = Math.round(Math.sin(t + Math.PI) * 3);

  canvas.fillEllipse(cx, 55, 16, 5, palette.shadow);
  drawLeg(canvas, cx - sign * 4, 45 + stride, 8);
  drawBoot(canvas, cx - sign * 4, 48 + stride, sign);
  drawLeg(canvas, cx + sign * 5, 45 - stride, 8);
  drawBoot(canvas, cx + sign * 5, 48 - stride, sign);

  drawBody(canvas, cx - sign * 1, 26, bob, "side");

  drawOutlinedRect(canvas, cx - sign * 16, 31 + bob - arm, 7, 14, palette.suitDark, palette.outline, 1);
  drawToolFist(canvas, cx - sign * (17 + arm), 43 + bob, frame % 3 === 0);
  drawOutlinedRect(canvas, cx + sign * 9, 31 + bob + arm, 7, 14, palette.suitDark, palette.outline, 1);
  drawToolFist(canvas, cx + sign * (17 + arm), 43 + bob, frame % 3 === 3);

  drawOutlinedEllipse(canvas, cx + sign * 2, 19 + bob, 12, 11, palette.skin, palette.outline, 2);
  canvas.fillRect(cx - sign * 9, 14 + bob, 12, 8, palette.hair);
  drawCapSide(canvas, cx + sign * 2, 19 + bob, sign);
  drawGogglesSide(canvas, cx + sign * 2, 20 + bob, sign);
  canvas.fillRect(cx + sign * 8, 25 + bob, 4 * sign, 1, palette.skinDark);
}

function drawDiagonal(canvas, frame, vertical, horizontal) {
  const sign = horizontal === "right" ? 1 : -1;
  const isDown = vertical === "down";
  const cx = 32;
  const t = (Math.PI * 2 * frame) / frameCount;
  const stride = Math.round(Math.sin(t) * 3);
  const bob = [0, -1, -2, -1, 0, -1][frame];
  const arm = Math.round(Math.sin(t + Math.PI) * 3);

  canvas.fillEllipse(cx, 55, 16, 5, palette.shadow);

  const frontShift = isDown ? stride : -stride;
  drawLeg(canvas, cx - sign * 5, 45 + frontShift, 8);
  drawBoot(canvas, cx - sign * 5, 48 + frontShift, sign);
  drawLeg(canvas, cx + sign * 5, 45 - frontShift, 8);
  drawBoot(canvas, cx + sign * 5, 48 - frontShift, sign);

  const bodyX = cx - sign * 1;
  if (!isDown) {
    drawOutlinedRect(canvas, bodyX - 9, 26 + bob, 18, 19, palette.backpack, palette.outline, 2);
    canvas.fillRect(bodyX - 6, 28 + bob, 12, 3, palette.backpackLight);
    canvas.fillRect(bodyX - 8, 36 + bob, 16, 4, palette.suitDark);
    canvas.fillRect(bodyX + sign * 8, 29 + bob, 5, 16, palette.suitDark);
  } else {
    drawOutlinedRect(canvas, bodyX - 10, 26 + bob, 20, 19, palette.suit, palette.outline, 2);
    canvas.fillRect(bodyX - 8, 28 + bob, 16, 3, palette.suitLight);
    canvas.fillRect(bodyX - 10, 38 + bob, 20, 4, palette.suitDark);
    canvas.fillRect(bodyX - 10, 41 + bob, 20, 3, palette.belt);
    canvas.fillRect(bodyX + sign * 2, 41 + bob, 5, 3, palette.brass);
  }

  canvas.fillRect(bodyX + sign * 8, 30 + bob, 5, 13, palette.backpack);
  canvas.fillRect(bodyX + sign * 9, 32 + bob, 2, 6, palette.backpackLight);

  drawOutlinedRect(canvas, cx - sign * 16, 31 + bob - arm, 7, 14, palette.suitDark, palette.outline, 1);
  drawToolFist(canvas, cx - sign * (17 + arm), 43 + bob, frame % 3 === 0);
  drawOutlinedRect(canvas, cx + sign * 9, 31 + bob + arm, 7, 14, palette.suitDark, palette.outline, 1);
  drawToolFist(canvas, cx + sign * (17 + arm), 43 + bob, frame % 3 === 3);

  drawOutlinedEllipse(canvas, cx + sign * 2, 19 + bob, 12, 11, isDown ? palette.skin : palette.hair, palette.outline, 2);
  if (isDown) {
    canvas.fillRect(cx - sign * 9, 14 + bob, 12, 8, palette.hair);
    drawCapSide(canvas, cx + sign * 2, 19 + bob, sign);
    drawGogglesSide(canvas, cx + sign * 2, 20 + bob, sign);
    drawGogglesFront(canvas, cx - sign * 2, 20 + bob);
    canvas.fillRect(cx + sign * 8, 25 + bob, 4 * sign, 1, palette.skinDark);
  } else {
    canvas.fillRect(cx - 9, 12 + bob, 18, 9, palette.hair);
    drawCapBack(canvas, cx + sign * 1, 19 + bob);
    canvas.fillRect(cx + sign * 7, 18 + bob, 5, 6, palette.skinDark);
    canvas.fillRect(cx - sign * 7, 20 + bob, 9, 2, palette.glassDark);
  }
}

function drawFrame(direction, frame) {
  const canvas = new PixelCanvas(frameSize, frameSize);
  if (direction === "down") drawFront(canvas, frame);
  if (direction === "up") drawBack(canvas, frame);
  if (direction === "right" || direction === "left") drawSide(canvas, frame, direction);
  if (direction === "down_right") drawDiagonal(canvas, frame, "down", "right");
  if (direction === "up_right") drawDiagonal(canvas, frame, "up", "right");
  if (direction === "up_left") drawDiagonal(canvas, frame, "up", "left");
  if (direction === "down_left") drawDiagonal(canvas, frame, "down", "left");
  return canvas;
}

function writePreviewHtml() {
  const html = `<!doctype html>
<html lang="zh-CN">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>43_002 机械师移动动画预览</title>
  <style>
    * { box-sizing: border-box; }
    body {
      margin: 0;
      min-height: 100vh;
      display: grid;
      place-items: center;
      background: #15191f;
      color: #edf4f8;
      font-family: "PingFang SC", "Microsoft YaHei", sans-serif;
    }
    main { width: min(880px, 100vw); padding: 32px; }
    h1 { margin: 0 0 20px; font-size: 22px; letter-spacing: 0; }
    .grid {
      display: grid;
      grid-template-columns: repeat(4, minmax(136px, 1fr));
      gap: 18px;
    }
    .preview {
      display: grid;
      justify-items: center;
      gap: 10px;
      padding: 18px 10px;
      border: 1px solid #314659;
      background:
        linear-gradient(45deg, #202832 25%, transparent 25%),
        linear-gradient(-45deg, #202832 25%, transparent 25%),
        linear-gradient(45deg, transparent 75%, #202832 75%),
        linear-gradient(-45deg, transparent 75%, #202832 75%),
        #17212b;
      background-position: 0 0, 0 8px, 8px -8px, -8px 0;
      background-size: 16px 16px;
      border-radius: 8px;
    }
    .sprite {
      width: 128px;
      height: 128px;
      background-image: url("./43_002_机械师_移动_精灵表.png");
      background-repeat: no-repeat;
      background-size: 768px 1024px;
      image-rendering: pixelated;
      animation: walk 0.5s steps(6) infinite;
    }
    .down { background-position-y: 0; }
    .down_right { background-position-y: -128px; }
    .right { background-position-y: -256px; }
    .up_right { background-position-y: -384px; }
    .up { background-position-y: -512px; }
    .up_left { background-position-y: -640px; }
    .left { background-position-y: -768px; }
    .down_left { background-position-y: -896px; }
    .label {
      min-width: 52px;
      padding: 3px 8px;
      border-radius: 999px;
      background: #253849;
      color: #edf4f8;
      text-align: center;
      font-size: 14px;
      font-weight: 700;
    }
    .meta { margin-top: 18px; color: #aeb8bf; font-size: 13px; line-height: 1.7; }
    @keyframes walk {
      from { background-position-x: 0; }
      to { background-position-x: -768px; }
    }
    @media (max-width: 680px) {
      main { padding: 18px; }
      .grid { grid-template-columns: repeat(2, minmax(136px, 1fr)); }
    }
  </style>
</head>
<body>
  <main>
    <h1>43_002 机械师移动动画</h1>
    <div class="grid">
      <div class="preview"><div class="sprite down"></div><div class="label">向下</div></div>
      <div class="preview"><div class="sprite down_right"></div><div class="label">右下</div></div>
      <div class="preview"><div class="sprite right"></div><div class="label">向右</div></div>
      <div class="preview"><div class="sprite up_right"></div><div class="label">右上</div></div>
      <div class="preview"><div class="sprite up"></div><div class="label">向上</div></div>
      <div class="preview"><div class="sprite up_left"></div><div class="label">左上</div></div>
      <div class="preview"><div class="sprite left"></div><div class="label">向左</div></div>
      <div class="preview"><div class="sprite down_left"></div><div class="label">左下</div></div>
    </div>
    <div class="meta">64x64 / 6 帧 / 8 方向 / 建议 12 FPS 循环播放</div>
  </main>
</body>
</html>
`;
  fs.writeFileSync(previewPath, html);
}

function writeMetadata() {
  const metadata = {
    id: "43_002",
    name: "机械师_移动动画",
    character: "机械师",
    style_reference: "俯视角像素动作游戏移动节奏；原创机械师造型，未复制具体商业素材",
    sprite_sheet: path.basename(spriteSheetPath),
    frame_directory: "frames_mechanic",
    frame_size: { width: frameSize, height: frameSize },
    sheet_grid: { columns: frameCount, rows: directions.length },
    directions: directions.map((direction) => ({
      key: direction.key,
      label: direction.label,
      row: direction.row,
      frames: frameCount,
      fps: 12,
      loop: true,
    })),
    godot_import_hint: {
      node: "AnimatedSprite2D 或 Sprite2D + AnimationPlayer",
      hframes: frameCount,
      vframes: directions.length,
      animation_names: directions.map((direction) => `walk_${direction.key}`),
    },
  };
  fs.writeFileSync(metadataPath, `${JSON.stringify(metadata, null, 2)}\n`);
}

fs.mkdirSync(frameDir, { recursive: true });
for (const direction of directions) {
  fs.mkdirSync(path.join(frameDir, direction.key), { recursive: true });
}

const sheet = new PixelCanvas(frameSize * frameCount, frameSize * directions.length);

for (const direction of directions) {
  for (let frame = 0; frame < frameCount; frame += 1) {
    const canvas = drawFrame(direction.key, frame);
    const frameName = `mechanic_walk_${direction.key}_${String(frame).padStart(2, "0")}.png`;
    savePng(path.join(frameDir, direction.key, frameName), canvas);
    sheet.copyFrom(canvas, frame * frameSize, direction.row * frameSize);
  }
}

savePng(spriteSheetPath, sheet);
writePreviewHtml();
writeMetadata();

console.log(`Generated ${directions.length * frameCount} mechanic frames`);
console.log(spriteSheetPath);
