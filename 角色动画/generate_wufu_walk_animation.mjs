import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { deflateSync } from "node:zlib";

const outDir = path.dirname(fileURLToPath(import.meta.url));
const frameDir = path.join(outDir, "frames");
const spriteSheetPath = path.join(outDir, "43_001_武夫_移动_精灵表.png");
const previewPath = path.join(outDir, "43_001_武夫_移动_预览.html");
const metadataPath = path.join(outDir, "43_001_武夫_移动_元数据.json");
const readmePath = path.join(outDir, "README.md");

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
  outline: [18, 19, 24, 255],
  deep: [35, 41, 47, 255],
  shadow: [0, 0, 0, 70],
  skin: [229, 178, 126, 255],
  skinDark: [185, 121, 76, 255],
  hair: [43, 34, 29, 255],
  hairLight: [92, 70, 55, 255],
  tunic: [42, 139, 101, 255],
  tunicDark: [26, 88, 72, 255],
  tunicLight: [88, 177, 128, 255],
  scarf: [190, 73, 63, 255],
  trim: [215, 154, 60, 255],
  pants: [37, 54, 70, 255],
  boot: [24, 27, 32, 255],
  eye: [28, 24, 22, 255],
  white: [239, 229, 204, 255],
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

  clone() {
    const canvas = new PixelCanvas(this.width, this.height);
    canvas.data.set(this.data);
    return canvas;
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
      for (let xx = left; xx < right; xx += 1) {
        this.blendPixel(xx, yy, color);
      }
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
        const sourceOffset = (y * source.width + x) * 4;
        const alpha = source.data[sourceOffset + 3];
        if (!alpha) continue;
        this.blendPixel(dx + x, dy + y, [
          source.data[sourceOffset],
          source.data[sourceOffset + 1],
          source.data[sourceOffset + 2],
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

function drawBoot(canvas, x, y, mirror = 1) {
  drawOutlinedRect(canvas, x - 3, y - 1, 7, 5, palette.boot, palette.outline, 1);
  canvas.fillRect(x + mirror * 1, y + 3, 5, 2, palette.deep);
}

function drawLeg(canvas, x, y, height = 8) {
  drawOutlinedRect(canvas, x - 3, y - height, 6, height, palette.pants, palette.outline, 1);
}

function drawFist(canvas, x, y) {
  drawOutlinedEllipse(canvas, x, y, 4, 4, palette.skin, palette.outline, 1);
  canvas.fillRect(x + 1, y, 2, 1, palette.skinDark);
}

function drawHairCapDown(canvas, cx, cy) {
  canvas.fillEllipse(cx, cy - 5, 12, 7, palette.hair);
  canvas.fillRect(cx - 10, cy - 11, 20, 7, palette.hair);
  canvas.fillRect(cx - 11, cy - 4, 3, 6, palette.hair);
  canvas.fillRect(cx + 8, cy - 4, 3, 6, palette.hair);
  canvas.fillRect(cx - 5, cy - 11, 8, 2, palette.hairLight);
}

function drawFaceDown(canvas, cx, cy) {
  canvas.fillRect(cx - 5, cy - 1, 2, 3, palette.eye);
  canvas.fillRect(cx + 4, cy - 1, 2, 3, palette.eye);
  canvas.fillRect(cx - 2, cy + 5, 5, 1, palette.skinDark);
  canvas.fillRect(cx - 4, cy + 6, 8, 1, [139, 71, 56, 255]);
}

function drawFaceSide(canvas, cx, cy, sign) {
  canvas.fillRect(cx + sign * 3, cy - 1, 2, 3, palette.eye);
  canvas.fillRect(cx + sign * 9, cy + 2, 3, 2, palette.skinDark);
  canvas.fillRect(cx + sign * 6, cy + 7, 4 * sign, 1, [139, 71, 56, 255]);
}

function drawBodyFrontBack(canvas, cx, cy, bob, direction) {
  const bodyY = cy + 26 + bob;
  drawOutlinedRect(canvas, cx - 10, bodyY, 20, 18, palette.tunic, palette.outline, 2);
  canvas.fillRect(cx - 8, bodyY + 2, 16, 3, palette.tunicLight);
  canvas.fillRect(cx - 10, bodyY + 12, 20, 4, palette.tunicDark);
  canvas.fillRect(cx - 9, bodyY + 15, 18, 3, palette.trim);
  if (direction === "down") {
    canvas.fillRect(cx - 3, bodyY + 1, 6, 14, palette.white);
    canvas.fillRect(cx - 2, bodyY + 5, 4, 8, palette.tunicDark);
  } else {
    canvas.fillRect(cx - 7, bodyY + 3, 14, 3, palette.tunicDark);
    canvas.fillRect(cx - 4, bodyY + 1, 8, 3, palette.scarf);
  }
}

function drawDown(canvas, frame) {
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

  drawOutlinedRect(canvas, cx - 18, 30 + bob + arm, 7, 15, palette.tunicDark, palette.outline, 1);
  drawOutlinedRect(canvas, cx + 11, 30 + bob - arm, 7, 15, palette.tunicDark, palette.outline, 1);
  drawFist(canvas, cx - 17, 44 + bob + arm);
  drawFist(canvas, cx + 17, 44 + bob - arm);

  drawBodyFrontBack(canvas, cx, 0, bob, "down");

  drawOutlinedEllipse(canvas, cx, 19 + bob, 12, 11, palette.skin, palette.outline, 2);
  drawHairCapDown(canvas, cx, 19 + bob);
  drawFaceDown(canvas, cx, 19 + bob);
}

function drawUp(canvas, frame) {
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

  drawOutlinedRect(canvas, cx - 18, 30 + bob - arm, 7, 15, palette.tunicDark, palette.outline, 1);
  drawOutlinedRect(canvas, cx + 11, 30 + bob + arm, 7, 15, palette.tunicDark, palette.outline, 1);
  drawFist(canvas, cx - 17, 44 + bob - arm);
  drawFist(canvas, cx + 17, 44 + bob + arm);

  drawBodyFrontBack(canvas, cx, 0, bob, "up");

  drawOutlinedEllipse(canvas, cx, 19 + bob, 12, 11, palette.hair, palette.outline, 2);
  canvas.fillRect(cx - 10, 11 + bob, 20, 8, palette.hair);
  canvas.fillRect(cx - 7, 22 + bob, 14, 6, palette.hair);
  canvas.fillRect(cx - 5, 10 + bob, 8, 2, palette.hairLight);
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

  drawOutlinedRect(canvas, cx - 8, 26 + bob, 18, 20, palette.tunic, palette.outline, 2);
  canvas.fillRect(cx - 6, 28 + bob, 14, 3, palette.tunicLight);
  canvas.fillRect(cx - 8, 38 + bob, 18, 5, palette.tunicDark);
  canvas.fillRect(cx - 7, 43 + bob, 16, 3, palette.trim);
  canvas.fillRect(cx + sign * 4, 28 + bob, 5 * sign, 9, palette.white);

  drawOutlinedRect(canvas, cx - sign * 15, 31 + bob - arm, 7, 14, palette.tunicDark, palette.outline, 1);
  drawFist(canvas, cx - sign * (16 + arm), 43 + bob - Math.abs(arm > 0 ? 1 : 0));
  drawOutlinedRect(canvas, cx + sign * 9, 31 + bob + arm, 7, 14, palette.tunicDark, palette.outline, 1);
  drawFist(canvas, cx + sign * (17 + arm), 43 + bob);

  drawOutlinedEllipse(canvas, cx + sign * 2, 19 + bob, 12, 11, palette.skin, palette.outline, 2);
  canvas.fillEllipse(cx - sign * 2, 14 + bob, 12, 7, palette.hair);
  canvas.fillRect(cx - 9, 9 + bob, 18, 8, palette.hair);
  canvas.fillRect(cx - sign * 9, 17 + bob, 8, 8, palette.hair);
  canvas.fillRect(cx + sign * 7, 20 + bob, 5, 4, palette.skinDark);
  canvas.fillRect(cx - 4, 9 + bob, 8, 2, palette.hairLight);
  drawFaceSide(canvas, cx + sign * 2, 19 + bob, sign);
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
    canvas.fillRect(bodyX + sign * 8, 29 + bob, 5, 16, palette.tunicDark);
  }
  drawOutlinedRect(canvas, bodyX - 9, 26 + bob, 19, 20, palette.tunic, palette.outline, 2);
  canvas.fillRect(bodyX - 7, 28 + bob, 15, 3, palette.tunicLight);
  canvas.fillRect(bodyX - 8, 38 + bob, 17, 5, palette.tunicDark);
  canvas.fillRect(bodyX - 7, 43 + bob, 16, 3, palette.trim);
  if (isDown) {
    canvas.fillRect(bodyX + sign * 1, 28 + bob, 6 * sign, 13, palette.white);
  } else {
    canvas.fillRect(bodyX - 5, 28 + bob, 10, 3, palette.scarf);
  }

  drawOutlinedRect(canvas, cx - sign * 16, 31 + bob - arm, 7, 14, palette.tunicDark, palette.outline, 1);
  drawFist(canvas, cx - sign * (17 + arm), 43 + bob - Math.abs(arm > 0 ? 1 : 0));
  drawOutlinedRect(canvas, cx + sign * 9, 31 + bob + arm, 7, 14, palette.tunicDark, palette.outline, 1);
  drawFist(canvas, cx + sign * (17 + arm), 43 + bob);

  drawOutlinedEllipse(canvas, cx + sign * 2, 19 + bob, 12, 11, isDown ? palette.skin : palette.hair, palette.outline, 2);
  if (isDown) {
    canvas.fillEllipse(cx - sign * 2, 14 + bob, 12, 7, palette.hair);
    canvas.fillRect(cx - 9, 9 + bob, 18, 8, palette.hair);
    canvas.fillRect(cx - sign * 9, 17 + bob, 8, 8, palette.hair);
    canvas.fillRect(cx + sign * 7, 20 + bob, 5, 4, palette.skinDark);
    canvas.fillRect(cx - 4, 9 + bob, 8, 2, palette.hairLight);
    drawFaceSide(canvas, cx + sign * 2, 19 + bob, sign);
    canvas.fillRect(cx - sign * 4, 19 + bob, 2, 3, palette.eye);
  } else {
    canvas.fillRect(cx - 10, 11 + bob, 20, 8, palette.hair);
    canvas.fillRect(cx - sign * 1, 21 + bob, 10 * sign, 6, palette.hair);
    canvas.fillRect(cx - 5, 10 + bob, 8, 2, palette.hairLight);
    canvas.fillRect(cx + sign * 8, 17 + bob, 4, 7, palette.skinDark);
  }
}

function drawFrame(direction, frame) {
  const canvas = new PixelCanvas(frameSize, frameSize);
  if (direction === "down") drawDown(canvas, frame);
  if (direction === "up") drawUp(canvas, frame);
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
  <title>43_001 武夫移动动画预览</title>
  <style>
    * { box-sizing: border-box; }
    body {
      margin: 0;
      min-height: 100vh;
      display: grid;
      place-items: center;
      background: #15191f;
      color: #f1ead9;
      font-family: "PingFang SC", "Microsoft YaHei", sans-serif;
    }
    main {
      width: min(880px, 100vw);
      padding: 32px;
    }
    h1 {
      margin: 0 0 20px;
      font-size: 22px;
      letter-spacing: 0;
    }
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
      border: 1px solid #33424d;
      background:
        linear-gradient(45deg, #202832 25%, transparent 25%),
        linear-gradient(-45deg, #202832 25%, transparent 25%),
        linear-gradient(45deg, transparent 75%, #202832 75%),
        linear-gradient(-45deg, transparent 75%, #202832 75%),
        #182029;
      background-position: 0 0, 0 8px, 8px -8px, -8px 0;
      background-size: 16px 16px;
      border-radius: 8px;
    }
    .sprite {
      width: 128px;
      height: 128px;
      background-image: url("./43_001_武夫_移动_精灵表.png");
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
      background: #273440;
      color: #f1ead9;
      text-align: center;
      font-size: 14px;
      font-weight: 700;
    }
    .meta {
      margin-top: 18px;
      color: #aeb8bf;
      font-size: 13px;
      line-height: 1.7;
    }
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
    <h1>43_001 武夫移动动画</h1>
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
    id: "43_001",
    name: "武夫_移动动画",
    character: "武夫",
    style_reference: "俯视角像素动作游戏移动节奏；原创角色造型，未复制具体商业素材",
    sprite_sheet: path.basename(spriteSheetPath),
    frame_directory: "frames",
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

function writeReadme() {
  const text = `# 角色移动动画

这些素材用于 2D 俯视角角色移动。节奏参考俯视角动作游戏中常见的短循环移动感：头身轻微上下弹动，左右脚交替前后错位，手臂反向摆动。角色造型和配色为原创，没有复刻具体商业角色。

## 文件

### 武夫

- \`43_001_武夫_移动_精灵表.png\`: 8 行 x 6 列透明 PNG 精灵表。
- \`frames/\`: 单帧透明 PNG，按方向分组。
- \`43_001_武夫_移动_预览.html\`: 本地浏览器预览。
- \`43_001_武夫_移动_元数据.json\`: 帧尺寸、方向行号和 Godot 导入建议。
- \`generate_wufu_walk_animation.mjs\`: 可复现生成脚本。

### 机械师

- \`43_002_机械师_移动_精灵表.png\`: 8 行 x 6 列透明 PNG 精灵表。
- \`frames_mechanic/\`: 单帧透明 PNG，按方向分组。
- \`43_002_机械师_移动_预览.html\`: 本地浏览器预览。
- \`43_002_机械师_移动_元数据.json\`: 帧尺寸、方向行号和 Godot 导入建议。
- \`generate_mechanic_walk_animation.mjs\`: 可复现生成脚本。

## Godot 使用参数

- 单帧尺寸：64x64
- 精灵表：Hframes = 6，Vframes = 8
- 行顺序：0 向下，1 右下，2 向右，3 右上，4 向上，5 左上，6 向左，7 左下
- 建议播放速度：12 FPS
- 动画名建议：\`walk_down\`、\`walk_down_right\`、\`walk_right\`、\`walk_up_right\`、\`walk_up\`、\`walk_up_left\`、\`walk_left\`、\`walk_down_left\`

`;
  fs.writeFileSync(readmePath, text);
}

fs.mkdirSync(frameDir, { recursive: true });
for (const direction of directions) {
  fs.mkdirSync(path.join(frameDir, direction.key), { recursive: true });
}

const sheet = new PixelCanvas(frameSize * frameCount, frameSize * directions.length);

for (const direction of directions) {
  for (let frame = 0; frame < frameCount; frame += 1) {
    const canvas = drawFrame(direction.key, frame);
    const frameName = `wufu_walk_${direction.key}_${String(frame).padStart(2, "0")}.png`;
    savePng(path.join(frameDir, direction.key, frameName), canvas);
    sheet.copyFrom(canvas, frame * frameSize, direction.row * frameSize);
  }
}

savePng(spriteSheetPath, sheet);
writePreviewHtml();
writeMetadata();
writeReadme();

console.log(`Generated ${directions.length * frameCount} frames`);
console.log(spriteSheetPath);
