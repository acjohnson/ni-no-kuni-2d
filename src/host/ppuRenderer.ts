/**
 * Project Hearth — Software Virtual PPU Renderer (320x224 Genesis / 256x224 SNES)
 *
 * Thin replaceable host renderer that consumes a pure PpuFramePacket from
 * HearthEngine and draws crisp nearest-neighbor 16-bit layers onto the canvas.
 */

import {
  BattleEnemyState,
  FP_ONE,
  LockKind,
  MAP_H,
  MAP_W,
  OamSprite,
  PartyMemberState,
  PpuFramePacket,
  T_LANTERN_POST,
  T_MTN_SKY,
  TILE_SIZE,
  VIEW_TILE_H,
  VIEW_TILE_W,
  WorldMapData,
} from '../core/types';
import { PixelAtlas } from './pixelAtlas';

export class SoftwarePpuRenderer {
  private scratchCanvas: HTMLCanvasElement;
  private scratchCtx: CanvasRenderingContext2D;
  private darkCanvas: HTMLCanvasElement;
  private darkCtx: CanvasRenderingContext2D;

  constructor() {
    this.scratchCanvas = document.createElement('canvas');
    this.scratchCanvas.width = 48;
    this.scratchCanvas.height = 48;
    this.scratchCtx = this.scratchCanvas.getContext('2d')!;
    this.darkCanvas = document.createElement('canvas');
    this.darkCanvas.width = 320;
    this.darkCanvas.height = 224;
    this.darkCtx = this.darkCanvas.getContext('2d')!;
  }

  public render(
    canvas: HTMLCanvasElement,
    packet: PpuFramePacket,
    map: WorldMapData,
    atlas: PixelAtlas,
    party: PartyMemberState[]
  ): void {
    if (canvas.width !== packet.width || canvas.height !== packet.height) {
      canvas.width = packet.width;
      canvas.height = packet.height;
    }

    atlas.setTerrainPalette(packet.terrainPalId);

    const ctx = canvas.getContext('2d');
    if (!ctx) return;
    ctx.imageSmoothingEnabled = false;

    ctx.save();
    ctx.clearRect(0, 0, packet.width, packet.height);
    ctx.translate(packet.shakeX, packet.shakeY);

    if (packet.mode === 'INTRO') {
      this.renderIntroScene(ctx, packet, atlas);
    } else if (
      packet.mode === 'BATTLE' ||
      packet.mode === 'BATTLE_VICTORY' ||
      packet.mode === 'BATTLE_DEFEAT'
    ) {
      this.renderBattleScene(ctx, packet, atlas, party);
    } else {
      this.renderOverworldScene(ctx, packet, map, atlas);
    }

    // White impact flash register (crunchy hitstop/crit feedback)
    if (packet.flashAlpha > 0.01) {
      ctx.fillStyle = `rgba(255, 250, 232, ${Math.min(0.65, packet.flashAlpha)})`;
      ctx.fillRect(-8, -8, packet.width + 16, packet.height + 16);
    }

    // Hardware-style scene crossfade veil (0..15)
    if (packet.fadeLevel > 0) {
      const alpha = Math.min(1, packet.fadeLevel / 15);
      ctx.fillStyle = `rgba(8, 7, 14, ${alpha.toFixed(3)})`;
      ctx.fillRect(-8, -8, packet.width + 16, packet.height + 16);
    }

    ctx.restore();
  }

  /* ========================================================================
   * OPENING CINEMA BACKDROP (Web renders full-HD painted plates in CinemaTheater)
   * ======================================================================== */
  private renderIntroScene(
    ctx: CanvasRenderingContext2D,
    packet: PpuFramePacket,
    _atlas: PixelAtlas
  ): void {
    ctx.fillStyle = '#08070F';
    ctx.fillRect(0, 0, packet.width, packet.height);
  }

  /* ========================================================================
   * OVERWORLD VIRTUAL PPU PIPELINE (BG1 -> OAM -> BG2 -> FX -> PROMPTS)
   * ======================================================================== */
  private renderOverworldScene(
    ctx: CanvasRenderingContext2D,
    packet: PpuFramePacket,
    map: WorldMapData,
    atlas: PixelAtlas
  ): void {
    const { width, height, camX, camY, frameCount } = packet;
    const mapW = map.width || packet.mapW || MAP_W;
    const mapH = map.height || packet.mapH || MAP_H;
    const animTick = frameCount >> 4;

    const startCol = Math.max(0, Math.floor(camX / TILE_SIZE));
    const endCol = Math.min(mapW - 1, Math.ceil((camX + width) / TILE_SIZE));
    const startRow = Math.max(0, Math.floor(camY / TILE_SIZE));
    const endRow = Math.min(mapH - 1, Math.ceil((camY + height) / TILE_SIZE));

    // 0. Parallax Mountain / Castle Aurelia Vista
    if (packet.hasMountainVista) {
      this.renderMountainVista(ctx, packet);
    }

    // 1. BG1 Terrain & Architecture Plane
    if (packet.isWorldMap) {
      // Real-time 24x18 toroidal sliding-window tile stream directly from C core!
      for (let vy = 0; vy < VIEW_TILE_H; vy++) {
        const sy = (packet.viewOriginY + vy) * TILE_SIZE - camY;
        const rowOff = vy * VIEW_TILE_W;
        for (let vx = 0; vx < VIEW_TILE_W; vx++) {
          const sx = (packet.viewOriginX + vx) * TILE_SIZE - camX;
          const tileId = packet.viewBg1[rowOff + vx];
          const img = atlas.getBg1Tile(tileId, animTick);
          ctx.drawImage(img, sx, sy, TILE_SIZE, TILE_SIZE);
        }
      }
    } else {
      for (let r = startRow; r <= endRow; r++) {
        const sy = r * TILE_SIZE - camY;
        const rowOff = r * mapW;
        for (let c = startCol; c <= endCol; c++) {
          const sx = c * TILE_SIZE - camX;
          const tileId = map.bg1[rowOff + c];
          if (tileId === T_MTN_SKY && packet.hasMountainVista) continue;
          const img = atlas.getBg1Tile(tileId, animTick);
          ctx.drawImage(img, sx, sy, TILE_SIZE, TILE_SIZE);
        }
      }
    }

    // 1.5. Impressive Multi-Tile Storybook Landmarks on the Evertale Toroidal Globe!
    if (packet.isWorldMap) {
      this.renderWorldMapLandmarks(ctx, packet, atlas);
    }

    // 2. OAM Layer 0 & Layer 1 (Y-Sorted Actors, Props & Auras)
    for (const spr of packet.sprites) {
      if (spr.layer <= 1) {
        this.drawOamSprite(ctx, spr, atlas, frameCount);
      }
    }

    // 3. BG2 Foreground Overhead Plane (Tree Canopies, Roof Eaves, Shrine Arch!)
    if (packet.isWorldMap) {
      for (let vy = 0; vy < VIEW_TILE_H; vy++) {
        const sy = (packet.viewOriginY + vy) * TILE_SIZE - camY;
        const rowOff = vy * VIEW_TILE_W;
        for (let vx = 0; vx < VIEW_TILE_W; vx++) {
          const bg2Id = packet.viewBg2[rowOff + vx];
          if (bg2Id === 0) continue;
          const tileImg = atlas.getBg2Tile(bg2Id);
          if (tileImg) {
            ctx.drawImage(tileImg, (packet.viewOriginX + vx) * TILE_SIZE - camX, sy, TILE_SIZE, TILE_SIZE);
          }
        }
      }
    } else {
      for (let r = startRow; r <= endRow; r++) {
        const sy = r * TILE_SIZE - camY;
        const rowOff = r * mapW;
        for (let c = startCol; c <= endCol; c++) {
          const bg2Id = map.bg2[rowOff + c];
          if (bg2Id === 0) continue;
          const tileImg = atlas.getBg2Tile(bg2Id);
          if (tileImg) {
            ctx.drawImage(tileImg, c * TILE_SIZE - camX, sy, TILE_SIZE, TILE_SIZE);
          }
        }
      }
    }

    // 4. OAM Layer 2 (Windmill Blades, Emotes, Heart-Orb Transfer!)
    for (const spr of packet.sprites) {
      if (spr.layer === 2) {
        this.drawOamSprite(ctx, spr, atlas, frameCount);
      }
    }

    // 4.2. Crisp World-Map Landmark Signposts when Rowan approaches a settlement or ruin
    if (packet.isWorldMap && packet.mode === 'EXPLORE') {
      this.renderWorldLandmarkBanners(ctx, packet);
    }

    // 4.5. Fishing Rod Line from Rowan to Bobber during MODE_FISHING
    if (packet.mode === 'FISHING' && packet.fishing.active) {
      const rx = Math.round(packet.heroWorldX - camX);
      const ry = Math.round(packet.heroWorldY - camY - 12);
      const bx = Math.round(packet.fishing.bobberX - camX);
      const by = Math.round(packet.fishing.bobberY - camY - 2);
      ctx.save();
      ctx.strokeStyle = '#E8DEC8';
      ctx.lineWidth = 1;
      ctx.beginPath();
      ctx.moveTo(rx, ry);
      ctx.quadraticCurveTo(
        Math.round((rx + bx) * 0.5),
        Math.min(ry, by) - 10,
        bx,
        by
      );
      ctx.stroke();
      ctx.restore();
    }

    // 5. World-Space Particles
    this.renderParticles(ctx, packet, false);

    // 5.5. Sunken Sewers Atmospheric Lantern Lighting Mask
    if (packet.zoneIsDark) {
      if (this.darkCanvas.width !== width || this.darkCanvas.height !== height) {
        this.darkCanvas.width = width;
        this.darkCanvas.height = height;
      }
      const dctx = this.darkCtx;
      dctx.globalCompositeOperation = 'source-over';
      dctx.clearRect(0, 0, width, height);
      dctx.fillStyle = packet.hasMountainVista
        ? 'rgba(10, 16, 38, 0.52)'
        : 'rgba(7, 11, 18, 0.76)';
      dctx.fillRect(0, 0, width, height);

      const punchLight = (lx: number, ly: number, r: number) => {
        const g = dctx.createRadialGradient(lx, ly, r * 0.18, lx, ly, r);
        g.addColorStop(0, 'rgba(0, 0, 0, 1)');
        g.addColorStop(0.62, 'rgba(0, 0, 0, 0.72)');
        g.addColorStop(1, 'rgba(0, 0, 0, 0)');
        dctx.fillStyle = g;
        dctx.beginPath();
        dctx.arc(lx, ly, r, 0, Math.PI * 2);
        dctx.fill();
      };

      dctx.globalCompositeOperation = 'destination-out';
      const flicker = Math.sin(frameCount * 0.12) * 3;
      punchLight(packet.heroWorldX - camX, packet.heroWorldY - camY - 6, 62 + flicker);
      for (let r = startRow; r <= endRow; r++) {
        const rowOff = r * mapW;
        for (let c = startCol; c <= endCol; c++) {
          if (map.bg1[rowOff + c] === T_LANTERN_POST) {
            punchLight(c * TILE_SIZE + 8 - camX, r * TILE_SIZE + 6 - camY, 48 + flicker * 0.6);
          }
        }
      }
      dctx.globalCompositeOperation = 'source-over';
      ctx.drawImage(this.darkCanvas, 0, 0);
    }

    // 6. Floating World Pop-up Text
    this.renderFloatingTexts(ctx, packet, true);

    // 7. Debug Hitboxes (if toggled in Feel Tuner)
    for (const b of packet.debugBoxes) {
      ctx.strokeStyle = b.color;
      ctx.lineWidth = 1;
      ctx.strokeRect(b.x + 0.5, b.y + 0.5, b.w, b.h);
    }

    // 8. Contextual Crisp Pixel Interact Bubble
    if (packet.interactPrompt.active) {
      const bob = Math.round(Math.sin(frameCount * 0.14) * 1.5);
      const text = `[A] ${packet.interactPrompt.label}`;
      ctx.font = 'bold 8px monospace';
      const tw = Math.ceil(ctx.measureText(text).width) + 8;
      const bx = Math.max(4, Math.min(width - tw - 4, Math.round(packet.interactPrompt.x - tw / 2)));
      const by = Math.max(4, Math.round(packet.interactPrompt.y - 12 + bob));

      ctx.fillStyle = '#181E2E';
      ctx.fillRect(bx, by, tw, 12);
      ctx.strokeStyle = '#F5B841';
      ctx.lineWidth = 1;
      ctx.strokeRect(bx + 0.5, by + 0.5, tw - 1, 11);
      ctx.fillStyle = '#FFF6CC';
      ctx.textBaseline = 'top';
      ctx.fillText(text, bx + 4, by + 2);
    }

    // 9. Zone Title Ribbon on Area Entry
    if (packet.zoneBannerFrames > 0 && packet.mode === 'EXPLORE' && packet.zoneName) {
      ctx.font = 'bold 8px monospace';
      const label = packet.zoneName.toUpperCase();
      const bw = Math.ceil(ctx.measureText(label).width) + 18;
      const bx = Math.round((width - bw) / 2);
      ctx.fillStyle = 'rgba(16, 18, 30, 0.88)';
      ctx.fillRect(bx, 6, bw, 14);
      ctx.strokeStyle = '#D4A454';
      ctx.lineWidth = 1;
      ctx.strokeRect(bx + 0.5, 6.5, bw - 1, 13);
      ctx.fillStyle = '#FFF3C4';
      ctx.textBaseline = 'top';
      ctx.fillText(label, bx + 9, 9);
    }
  }

  /* ========================================================================
   * PARALLAX SNOW-DUSTED ALPINE MOUNTAIN VISTA (Morning / Afternoon / Dusk / Night)
   * ======================================================================== */
  private renderMountainVista(ctx: CanvasRenderingContext2D, packet: PpuFramePacket): void {
    const { width, camX, camY, frameCount, timeOfDay } = packet;
    const horizonH = Math.min(packet.height, Math.max(88, 128 - Math.floor(camY * 0.25)));

    // 1. Time-of-Day Sky Gradient Bands
    const skyPalettes: readonly (readonly string[])[] = [
      ['#3A5C88', '#6B8EB5', '#B9978A', '#ECC89E'], // 0 = Morning Rose-Gold Dawn
      ['#2E649E', '#528EC4', '#86B8DF', '#D6E2E8'], // 1 = Crisp Autumn Afternoon
      ['#2B204A', '#6A3656', '#C25F4A', '#EEA05B'], // 2 = Copper-Amber Dusk
      ['#0B1228', '#142042', '#1D315C', '#2A4370'], // 3 = Moonlit Indigo Night
    ];
    const bands = skyPalettes[timeOfDay] ?? skyPalettes[1];
    const bandH = Math.ceil(horizonH / 4);
    for (let i = 0; i < 4; i++) {
      ctx.fillStyle = bands[i];
      ctx.fillRect(0, i * bandH, width, bandH + 1);
    }

    // 2. Celestial Body / Drifting Autumn Clouds
    if (timeOfDay === 3) {
      // Twinkling alpine stars + harvest crescent moon
      ctx.fillStyle = '#E8F0FF';
      for (let i = 0; i < 20; i++) {
        const sx = (i * 47 + 13) % width;
        const sy = (i * 19 + 5) % Math.max(24, horizonH - 28);
        if (((frameCount >> 4) + i) % 5 !== 0) {
          ctx.fillRect(sx, sy, 1, 1);
        }
      }
      ctx.fillStyle = '#FFF5D0';
      ctx.fillRect(width - 54, 12, 12, 12);
      ctx.fillRect(width - 52, 10, 8, 16);
      ctx.fillStyle = '#142042';
      ctx.fillRect(width - 49, 11, 7, 10);
    } else {
      const cloudCol = timeOfDay === 2 ? '#F6B882' : timeOfDay === 0 ? '#FBE3C6' : '#F4F7FA';
      ctx.fillStyle = cloudCol;
      for (let i = 0; i < 4; i++) {
        const cx = (((i * 98 + Math.floor(frameCount * 0.08) - Math.floor(camX * 0.06)) % (width + 64)) + (width + 64)) % (width + 64) - 32;
        const cy = 9 + (i & 1) * 11;
        ctx.fillRect(cx, cy, 30, 5);
        ctx.fillRect(cx + 6, cy - 3, 18, 4);
      }
    }

    // 3. Far Snow-Dusted Alpine Peaks (Parallax 0.14x)
    const farPeakCol =
      timeOfDay === 3 ? '#1F2D4E' : timeOfDay === 2 ? '#543554' : timeOfDay === 0 ? '#5E6B8A' : '#586E8C';
    const snowCapCol =
      timeOfDay === 3 ? '#B4C6E6' : timeOfDay === 2 ? '#F4C6A0' : timeOfDay === 0 ? '#FBEAD6' : '#F2F6FA';
    const farScroll = Math.floor(camX * 0.14);
    for (let x = 0; x < width; x += 4) {
      const wx = x + farScroll;
      const ridge =
        Math.abs(((wx % 96) + 96) % 96 - 48) * 0.65 +
        Math.abs((((wx + 34) % 64) + 64) % 64 - 32) * 0.45;
      const peakY = Math.max(12, Math.round(24 + ridge * 0.72));
      ctx.fillStyle = farPeakCol;
      ctx.fillRect(x, peakY, 4, horizonH - peakY + 8);
      if (peakY < 42) {
        ctx.fillStyle = snowCapCol;
        ctx.fillRect(x, peakY, 4, Math.min(10, 42 - peakY));
      }
    }

    // 4. Midground Ridge — Evertale Sunlit Castle Aurelia Skyline vs. Alderbrook Autumn Foothills
    const isEvertaleVista = packet.zoneId === 9 || packet.zoneId === 10;
    const midCol = isEvertaleVista
      ? '#4A8E54'
      : timeOfDay === 3
        ? '#15243A'
        : timeOfDay === 2
          ? '#4A2B36'
          : timeOfDay === 0
            ? '#3E5448'
            : '#355444';
    const midMaple = isEvertaleVista
      ? '#6AB868'
      : timeOfDay === 3
        ? '#3B2846'
        : timeOfDay === 2
          ? '#A64A32'
          : timeOfDay === 0
            ? '#B8663A'
            : '#B84A32';
    const midScroll = Math.floor(camX * 0.28);
    for (let x = 0; x < width; x += 4) {
      const wx = x + midScroll;
      const hill = Math.round(Math.sin(wx * 0.025) * 8 + Math.cos(wx * 0.052) * 4);
      const hy = Math.max(38, (isEvertaleVista ? 58 : 56) + hill);
      ctx.fillStyle = midCol;
      ctx.fillRect(x, hy, 4, horizonH - hy + 8);
      if (((wx >> 3) & 3) === 1) {
        ctx.fillStyle = midMaple;
        ctx.fillRect(x, hy + 2, 4, 6);
      }
    }

    // 5. Distant Gleaming Castle Aurelia Spire Silhouette above Sunhill Market & Castle Gate
    if (isEvertaleVista) {
      const cx = Math.round(width * 0.5 - (camX - 180) * 0.12);
      const cy = 22;
      ctx.fillStyle = '#DCE6F2';
      ctx.fillRect(cx - 32, cy + 20, 64, 24);
      ctx.fillRect(cx - 10, cy + 8, 20, 34);
      ctx.fillRect(cx - 28, cy + 14, 12, 26);
      ctx.fillRect(cx + 16, cy + 14, 12, 26);
      ctx.fillStyle = '#F7FAFF';
      ctx.fillRect(cx - 8, cy + 10, 6, 30);
      ctx.fillRect(cx - 26, cy + 16, 4, 22);
      ctx.fillRect(cx + 18, cy + 16, 4, 22);
      // Azure conical turrets & gold pennants
      ctx.fillStyle = '#3874B8';
      ctx.fillRect(cx - 12, cy + 4, 24, 5);
      ctx.fillRect(cx - 8, cy, 16, 5);
      ctx.fillRect(cx - 4, cy - 4, 8, 5);
      ctx.fillRect(cx - 30, cy + 10, 16, 5);
      ctx.fillRect(cx - 26, cy + 6, 8, 5);
      ctx.fillRect(cx + 14, cy + 10, 16, 5);
      ctx.fillRect(cx + 18, cy + 6, 8, 5);
      const wave = (frameCount >> 4) & 1;
      ctx.fillStyle = '#F5C45A';
      ctx.fillRect(cx - 1, cy - 8, 6 + wave, 3);
      ctx.fillRect(cx - 23, cy + 2, 5, 2);
      ctx.fillRect(cx + 21, cy + 2, 5, 2);
    }
  }

  /* ========================================================================
   * EVERTALE OPEN WORLD MULTI-TILE STORYBOOK LANDMARKS (Chrono Trigger / FFIV Scale)
   * ======================================================================== */
  private renderWorldMapLandmarks(
    ctx: CanvasRenderingContext2D,
    packet: PpuFramePacket,
    atlas: PixelAtlas
  ): void {
    const { width, height, camX, camY, frameCount } = packet;
    const globePxW = packet.globeW * TILE_SIZE;
    const globePxH = packet.globeH * TILE_SIZE;
    const wave = (frameCount >> 4) & 1;

    const wrapDelta = (d: number, span: number) => {
      const half = span >> 1;
      let r = ((d + half) % span + span) % span - half;
      return r;
    };
    const toScreenX = (worldPx: number) =>
      Math.round((width >> 1) + wrapDelta(worldPx - (camX + (width >> 1)), globePxW));
    const toScreenY = (worldPx: number) =>
      Math.round((height >> 1) + wrapDelta(worldPx - (camY + (height >> 1)), globePxH));
    const inView = (x: number, y: number, w: number, h: number) =>
      x + w >= -24 && x <= width + 24 && y + h >= -24 && y <= height + 24;

    const drawCottage = (x: number, y: number, w: number, h: number, roofCol: string, roofHi: string) => {
      ctx.fillStyle = 'rgba(18, 26, 20, 0.35)';
      ctx.fillRect(x + 2, y + h - 2, w, 4);
      ctx.fillStyle = '#1C1B24';
      ctx.fillRect(x, y, w, h);
      ctx.fillStyle = '#F2E5C8';
      ctx.fillRect(x + 1, y + Math.floor(h * 0.45), w - 2, h - Math.floor(h * 0.45) - 1);
      ctx.fillStyle = roofCol;
      ctx.fillRect(x - 1, y + 1, w + 2, Math.floor(h * 0.48));
      ctx.fillStyle = roofHi;
      ctx.fillRect(x + 1, y, w - 2, 3);
      ctx.fillStyle = '#6B462B';
      ctx.fillRect(x + Math.floor(w * 0.4), y + h - 6, 4, 5);
      ctx.fillStyle = '#F5C45A';
      ctx.fillRect(x + 3, y + Math.floor(h * 0.58), 3, 3);
    };

    const lmPos = (idx: number): [number, number] | null => {
      const lm = packet.landmarks[idx];
      if (!lm) return null;
      return [toScreenX(lm.tx * TILE_SIZE + 8), toScreenY(lm.ty * TILE_SIZE + 8)];
    };

    // 0. HOLLOW OAK GATE (idx 0)
    {
      const p = lmPos(0);
      if (p) {
        const bx = p[0] - 24;
        const by = p[1] - 34;
        if (inView(bx, by, 48, 46)) {
          ctx.fillStyle = '#6E462C';
          ctx.fillRect(bx + 14, by + 18, 20, 20);
          ctx.fillStyle = wave ? '#74E8F5' : '#FFE07D';
          ctx.fillRect(bx + 20, by + 26, 8, 12);
          ctx.fillStyle = '#3E7A38';
          ctx.fillRect(bx + 4, by + 4, 40, 16);
          ctx.fillStyle = '#D48A34';
          ctx.fillRect(bx + 8, by + 3, 32, 11);
          ctx.fillStyle = '#F5C45A';
          ctx.fillRect(bx + 14, by + 2, 20, 6);
        }
      }
    }

    // 1. WINDMILL HAMLET (idx 1)
    {
      const p = lmPos(1);
      if (p) {
        const bx = p[0] - 28;
        const by = p[1] - 32;
        if (inView(bx, by, 58, 46)) {
          drawCottage(bx + 2, by + 20, 18, 18, '#B8543E', '#D87258');
          drawCottage(bx + 34, by + 22, 18, 16, '#A84836', '#CE684E');
          ctx.fillStyle = '#B8C0CC';
          ctx.fillRect(bx + 20, by + 12, 14, 24);
          ctx.fillStyle = '#3B5998';
          ctx.fillRect(bx + 17, by + 4, 20, 9);
          const sails = atlas.getSprite('windmill_blades', 0, 0);
          ctx.save();
          ctx.translate(bx + 27, by + 14);
          ctx.rotate(((frameCount * 0.9) * Math.PI) / 180);
          ctx.drawImage(sails, -13, -13, 26, 26);
          ctx.restore();
        }
      }
    }

    // 2. SUNHILL MARKET (idx 2)
    {
      const p = lmPos(2);
      if (p) {
        const bx = p[0] - 40;
        const by = p[1] - 44;
        if (inView(bx, by, 84, 58)) {
          ctx.fillStyle = '#D4C8B4';
          ctx.fillRect(bx + 4, by + 42, 72, 8);
          drawCottage(bx + 4, by + 16, 20, 22, '#C8523E', '#E67658');
          drawCottage(bx + 56, by + 14, 20, 24, '#B84A38', '#DC6B52');
          drawCottage(bx + 26, by + 4, 26, 22, '#3B6EA8', '#5C94CE');
          ctx.fillStyle = '#F5C45A';
          ctx.fillRect(bx + 36, by - 2, 6, 6);
          for (const ax of [bx + 12, bx + 52]) {
            ctx.fillStyle = '#D6483C';
            ctx.fillRect(ax + 1, by + 32, 14, 4);
            ctx.fillStyle = '#FFF4DC';
            ctx.fillRect(ax + 4, by + 32, 3, 4);
            ctx.fillRect(ax + 10, by + 32, 3, 4);
          }
        }
      }
    }

    // 3. CASTLE AURELIA (idx 3) — Grand White-Marble Citadel
    {
      const p = lmPos(3);
      if (p) {
        const bx = p[0] - 56;
        const by = p[1] - 60;
        if (inView(bx, by, 112, 78)) {
          ctx.fillStyle = 'rgba(18, 28, 36, 0.42)';
          ctx.fillRect(bx + 4, by + 62, 104, 14);
          ctx.fillStyle = '#202636';
          ctx.fillRect(bx + 6, by + 34, 100, 34);
          ctx.fillStyle = '#C9D5E6';
          ctx.fillRect(bx + 8, by + 36, 96, 30);
          ctx.fillStyle = '#EEF4FC';
          ctx.fillRect(bx + 10, by + 38, 92, 6);
          for (const tx of [bx + 8, bx + 84]) {
            ctx.fillStyle = '#202636';
            ctx.fillRect(tx, by + 18, 20, 48);
            ctx.fillStyle = '#D8E3F2';
            ctx.fillRect(tx + 2, by + 20, 16, 44);
            ctx.fillStyle = '#1E3C6E';
            ctx.fillRect(tx - 2, by + 13, 24, 6);
            ctx.fillStyle = '#3672B8';
            ctx.fillRect(tx, by + 9, 20, 6);
            ctx.fillStyle = '#5CA4E8';
            ctx.fillRect(tx + 4, by + 4, 12, 6);
            ctx.fillStyle = '#F5C45A';
            ctx.fillRect(tx + 10, by - 3, 7 + wave, 3);
          }
          ctx.fillStyle = '#202636';
          ctx.fillRect(bx + 36, by + 8, 40, 56);
          ctx.fillStyle = '#E4ECF8';
          ctx.fillRect(bx + 38, by + 10, 36, 52);
          ctx.fillStyle = '#3672B8';
          ctx.fillRect(bx + 40, by - 4, 32, 10);
          ctx.fillStyle = '#5CA4E8';
          ctx.fillRect(bx + 46, by - 10, 20, 7);
          ctx.fillStyle = '#F5C45A';
          ctx.fillRect(bx + 54, by - 15, 4, 6);
          ctx.fillRect(bx + 56, by - 15, 9 + wave, 3);
          ctx.fillStyle = '#5E3B24';
          ctx.fillRect(bx + 48, by + 46, 16, 22);
        }
      }
    }

    // 4. BRAMBLEWOOD CROSSING (idx 4)
    {
      const p = lmPos(4);
      if (p) {
        const bx = p[0] - 28;
        const by = p[1] - 32;
        if (inView(bx, by, 58, 44)) {
          for (const [tx, ty, sz] of [[4, 8, 22], [32, 8, 22], [16, 0, 26]] as const) {
            ctx.fillStyle = '#285C36';
            ctx.fillRect(bx + tx + 2, by + ty + 2, sz - 4, sz - 4);
            ctx.fillStyle = '#448C48';
            ctx.fillRect(bx + tx + 4, by + ty + 3, sz - 8, Math.floor(sz * 0.55));
          }
          ctx.fillStyle = '#9EAAB8';
          ctx.fillRect(bx + 17, by + 25, 5, 14);
          ctx.fillRect(bx + 36, by + 25, 5, 14);
        }
      }
    }

    // 5. SUNKEN WATCHTOWER (idx 5)
    {
      const p = lmPos(5);
      if (p) {
        const bx = p[0] - 32;
        const by = p[1] - 34;
        if (inView(bx, by, 66, 46)) {
          ctx.fillStyle = '#7F8A9C';
          ctx.fillRect(bx + 8, by + 20, 10, 18);
          ctx.fillRect(bx + 46, by + 24, 10, 14);
          ctx.fillRect(bx + 24, by + 6, 16, 32);
          ctx.fillStyle = '#4B8E54';
          ctx.fillRect(bx + 24, by + 6, 6, 4);
        }
      }
    }

    // 6. WHISKERBURGH (idx 6)
    {
      const p = lmPos(6);
      if (p) {
        const bx = p[0] - 40;
        const by = p[1] - 38;
        if (inView(bx, by, 80, 52)) {
          drawCottage(bx + 4, by + 16, 20, 20, '#8A4E74', '#B4729B');
          drawCottage(bx + 52, by + 16, 20, 20, '#8A4E74', '#B4729B');
          drawCottage(bx + 24, by + 6, 28, 26, '#D49A38', '#F2C05A');
          ctx.fillStyle = '#68BBE8';
          ctx.fillRect(bx + 12, by + 38, 52, 5);
        }
      }
    }

    // 7. HEARTLAND TIMBER PIER (idx 7)
    {
      const p = lmPos(7);
      if (p) {
        const bx = p[0] - 24;
        const by = p[1] - 22;
        if (inView(bx, by, 52, 40)) {
          drawCottage(bx + 4, by + 4, 22, 18, '#3B6EA8', '#5C94CE');
          ctx.fillStyle = '#8C5B32';
          ctx.fillRect(bx + 18, by + 22, 26, 8);
          ctx.fillStyle = '#C48B50';
          ctx.fillRect(bx + 20, by + 23, 22, 5);
          ctx.fillStyle = '#F5C45A';
          ctx.fillRect(bx + 40, by + 15, 5 + wave, 3);
        }
      }
    }

    // 8. SUNCREST CORAL COVE (idx 8)
    {
      const p = lmPos(8);
      if (p) {
        const bx = p[0] - 34;
        const by = p[1] - 30;
        if (inView(bx, by, 72, 48)) {
          drawCottage(bx + 6, by + 14, 22, 20, '#C98A36', '#F0BE5C');
          drawCottage(bx + 40, by + 12, 22, 20, '#D4565A', '#F28686');
          ctx.fillStyle = '#2A4E6E';
          ctx.fillRect(bx + 28, by + 2, 14, 12);
          ctx.fillStyle = '#FFF5D6';
          ctx.fillRect(bx + 32, by + 5, 6, 5);
        }
      }
    }

    // 9. OASIS OF BABANA (idx 9)
    {
      const p = lmPos(9);
      if (p) {
        const bx = p[0] - 40;
        const by = p[1] - 38;
        if (inView(bx, by, 84, 54)) {
          ctx.fillStyle = '#E8C584';
          ctx.fillRect(bx + 8, by + 18, 66, 24);
          ctx.fillStyle = '#F5D042';
          ctx.fillRect(bx + 28, by + 6, 26, 14);
          ctx.fillRect(bx + 10, by + 12, 14, 10);
          ctx.fillRect(bx + 58, by + 12, 14, 10);
          ctx.fillStyle = '#48C4D4';
          ctx.fillRect(bx + 26, by + 34, 30, 8);
          ctx.fillStyle = '#D6483C';
          ctx.fillRect(bx + 14, by + 26, 12, 5);
          ctx.fillRect(bx + 56, by + 26, 12, 5);
        }
      }
    }

    // 10. FROSTMERE NOEL VILLAGE (idx 10)
    {
      const p = lmPos(10);
      if (p) {
        const bx = p[0] - 36;
        const by = p[1] - 32;
        if (inView(bx, by, 76, 48)) {
          drawCottage(bx + 4, by + 16, 20, 20, '#C83E3E', '#FFFFFF');
          drawCottage(bx + 50, by + 16, 20, 20, '#2E7D4A', '#FFFFFF');
          ctx.fillStyle = '#246B3C';
          ctx.fillRect(bx + 32, by + 6, 12, 22);
          ctx.fillStyle = wave ? '#FFE878' : '#F25454';
          ctx.fillRect(bx + 37, by + 3, 3, 3);
          ctx.fillRect(bx + 34, by + 13, 2, 2);
          ctx.fillRect(bx + 40, by + 18, 2, 2);
        }
      }
    }

    // 11. CANOPY TREETOP VILLAGE (idx 11)
    {
      const p = lmPos(11);
      if (p) {
        const bx = p[0] - 34;
        const by = p[1] - 30;
        if (inView(bx, by, 72, 48)) {
          ctx.fillStyle = '#2E6E36';
          ctx.fillRect(bx + 2, by + 10, 68, 28);
          ctx.fillStyle = '#9C6838';
          ctx.fillRect(bx + 10, by + 18, 52, 6);
          drawCottage(bx + 12, by + 4, 20, 16, '#4A8C3C', '#78BA58');
          drawCottage(bx + 40, by + 4, 20, 16, '#4A8C3C', '#78BA58');
        }
      }
    }

    // 12. CINDERPEAK CALDERA GATE (idx 12)
    {
      const p = lmPos(12);
      if (p) {
        const bx = p[0] - 36;
        const by = p[1] - 34;
        if (inView(bx, by, 74, 50)) {
          ctx.fillStyle = '#2A2226';
          ctx.fillRect(bx + 8, by + 10, 58, 34);
          ctx.fillStyle = '#4A3638';
          ctx.fillRect(bx + 16, by + 2, 42, 36);
          ctx.fillStyle = wave ? '#F5A623' : '#E84A24';
          ctx.fillRect(bx + 28, by + 24, 18, 18);
          ctx.fillRect(bx + 22, by + 6, 30, 5);
        }
      }
    }

    // 13. AETHERIS CITADEL SPIRE (idx 13)
    {
      const p = lmPos(13);
      if (p) {
        const bx = p[0] - 34;
        const by = p[1] - 40;
        if (inView(bx, by, 70, 56)) {
          ctx.fillStyle = '#28364A';
          ctx.fillRect(bx + 12, by + 18, 46, 28);
          ctx.fillStyle = '#B8CCE0';
          ctx.fillRect(bx + 24, by + 2, 22, 42);
          ctx.fillStyle = wave ? '#5AD2F4' : '#B8F6FF';
          ctx.fillRect(bx + 32, by + 6, 6, 32);
          ctx.fillRect(bx + 14, by + 26, 42, 3);
        }
      }
    }
  }

  private renderWorldLandmarkBanners(ctx: CanvasRenderingContext2D, packet: PpuFramePacket): void {
    const { width, height, camX, camY, heroWorldX, heroWorldY } = packet;
    const globePxW = packet.globeW * TILE_SIZE;
    const globePxH = packet.globeH * TILE_SIZE;
    const wrapDelta = (d: number, span: number) => {
      const half = span >> 1;
      return ((d + half) % span + span) % span - half;
    };

    ctx.font = 'bold 8px monospace';
    ctx.textBaseline = 'top';
    for (const lm of packet.landmarks) {
      const lx = lm.tx * TILE_SIZE + 8;
      const ly = lm.ty * TILE_SIZE + 8;
      const dx = Math.abs(wrapDelta(heroWorldX - lx, globePxW));
      const dy = Math.abs(wrapDelta(heroWorldY - ly, globePxH));
      if (dx + dy > 118) continue;
      const label = lm.name.toUpperCase();
      const tw = Math.ceil(ctx.measureText(label).width) + 10;
      const screenX = (width >> 1) + wrapDelta(lx - (camX + (width >> 1)), globePxW);
      const screenY = (height >> 1) + wrapDelta(ly - (camY + (height >> 1)), globePxH);
      const sx = Math.max(4, Math.min(width - tw - 4, Math.round(screenX - tw / 2)));
      const sy = Math.max(4, Math.min(height - 16, Math.round(screenY - 34)));
      ctx.fillStyle = 'rgba(18, 22, 34, 0.88)';
      ctx.fillRect(sx, sy, tw, 12);
      ctx.strokeStyle = lm.enterable ? '#E8BE5E' : '#6CC8E8';
      ctx.lineWidth = 1;
      ctx.strokeRect(sx + 0.5, sy + 0.5, tw - 1, 11);
      ctx.fillStyle = lm.enterable ? '#FFF5D0' : '#D8F2FF';
      ctx.fillText(label, sx + 5, sy + 2);
    }
  }

  /* ========================================================================
   * IN-PLACE SEAMLESS 16-BIT BATTLE ARENA PIPELINE
   * ======================================================================== */
  private renderBattleScene(
    ctx: CanvasRenderingContext2D,
    packet: PpuFramePacket,
    atlas: PixelAtlas,
    party: PartyMemberState[]
  ): void {
    const { width, height, frameCount, battle } = packet;
    const biome = battle.biome;
    const isShrine = biome === 'SHRINE';
    const isSewer = biome === 'SEWER';
    const isBoarhold = biome === 'BOARHOLD';
    const isDesert = biome === 'DESERT';
    const isParadise = biome === 'PARADISE';

    // 1. Layered 16-bit Sky & Horizon Backdrop (cohesive with world biome!)
    const skyBands = isSewer
      ? ['#0C141A', '#13212A', '#1B2E38', '#233B44']
      : isBoarhold
        ? ['#241418', '#422024', '#68302A', '#924834']
        : isDesert
          ? ['#346AA6', '#66A0D4', '#E6B87A', '#F6D79A']
          : isParadise
            ? ['#2A8CD4', '#4EB8E8', '#84DCF4', '#C6F4FA']
            : isShrine
              ? ['#1D2236', '#28314C', '#384666', '#4E5D7B']
              : ['#5AA0D8', '#7BBCE8', '#A6D7F2', '#DCEFC8'];
    const bandH = 19;
    skyBands.forEach((col, i) => {
      ctx.fillStyle = col;
      ctx.fillRect(0, i * bandH, width, bandH + 1);
    });

    if (isSewer) {
      // Arched sewer vault bricks & glowing lantern sconces
      ctx.fillStyle = '#16252E';
      for (let x = 0; x < width; x += 64) {
        ctx.fillRect(x, 0, 14, 78);
        ctx.fillStyle = '#2B424D';
        ctx.fillRect(x + 2, 0, 4, 78);
        ctx.fillStyle = '#F5C45A';
        ctx.fillRect(x + 4, 28, 6, 8);
        ctx.fillStyle = '#16252E';
      }
    } else if (isBoarhold) {
      // Fortress crenellations & torchlight braziers
      ctx.fillStyle = '#342426';
      ctx.fillRect(0, 44, width, 34);
      for (let x = 0; x < width; x += 24) {
        ctx.fillRect(x, 34, 14, 12);
      }
      ctx.fillStyle = '#E88838';
      ctx.fillRect(46, 26, 6, 8);
      ctx.fillRect(width - 54, 26, 6, 8);
    } else if (isDesert) {
      // Blazing desert sun & golden Babana dunes
      ctx.fillStyle = '#FFF5C4';
      ctx.fillRect(width - 68, 10, 18, 18);
      ctx.fillStyle = '#D4924A';
      for (let x = 0; x < width; x += 4) {
        const h = Math.round(14 + Math.sin(x * 0.025) * 8 + Math.cos(x * 0.04) * 4);
        ctx.fillRect(x, 76 - h, 4, h + 12);
      }
    } else if (isParadise) {
      // Turquoise lagoon horizon & sunlit coral archipelago
      ctx.fillStyle = '#F6FCFF';
      for (let i = 0; i < 4; i++) {
        const cx = ((i * 92 + Math.floor(frameCount * 0.14)) % (width + 64)) - 32;
        ctx.fillRect(cx, 12 + (i & 1) * 10, 32, 6);
      }
      ctx.fillStyle = '#228EC4';
      ctx.fillRect(0, 58, width, 20);
      ctx.fillStyle = '#5AE0F2';
      ctx.fillRect(0, 68, width, 10);
    } else {
      // Drifting pixel clouds
      ctx.fillStyle = isShrine ? '#495472' : '#F4F7EE';
      for (let i = 0; i < 4; i++) {
        const cx = ((i * 96 + Math.floor(frameCount * 0.15)) % (width + 64)) - 32;
        const cy = 14 + (i % 2) * 14;
        ctx.fillRect(cx, cy, 34, 7);
        ctx.fillRect(cx + 6, cy - 4, 20, 5);
      }

      // Distant rolling hills silhouette + windmill / shrine monoliths
      ctx.fillStyle = isShrine ? '#2A3548' : '#4A884E';
      for (let x = 0; x < width; x += 4) {
        const h = Math.round(14 + Math.sin(x * 0.03) * 6 + Math.cos(x * 0.015) * 5);
        ctx.fillRect(x, 76 - h, 4, h + 12);
      }

      if (isShrine) {
        ctx.fillStyle = '#3E4656';
        ctx.fillRect(40, 34, 14, 48);
        ctx.fillRect(width - 62, 30, 16, 52);
        ctx.fillStyle = '#748094';
        ctx.fillRect(42, 36, 4, 44);
        ctx.fillRect(width - 60, 32, 5, 48);
      } else {
        ctx.fillStyle = '#6A7586';
        ctx.fillRect(width - 78, 36, 16, 36);
        ctx.fillStyle = '#3B5998';
        ctx.fillRect(width - 80, 30, 20, 8);
        const sails = atlas.getSprite('windmill_blades', 0, 0);
        ctx.save();
        ctx.translate(width - 70, 36);
        ctx.rotate(((frameCount * 0.8) * Math.PI) / 180);
        ctx.drawImage(sails, -12, -12, 24, 24);
        ctx.restore();
      }
    }

    // 2. Ground Battle Stage Plane
    const groundY = 76;
    ctx.fillStyle = isSewer
      ? '#21333B'
      : isBoarhold
        ? '#584440'
        : isDesert
          ? '#D8A252'
          : isParadise
            ? '#E6CFA0'
            : isShrine
              ? '#3D4555'
              : '#5EA055';
    ctx.fillRect(0, groundY, width, height - groundY);

    // Textured perspective strips & biome floor landmarks
    if (isSewer) {
      // Glowing moss-green water canal across top of arena
      ctx.fillStyle = '#1E645C';
      ctx.fillRect(0, groundY + 4, width, 16);
      ctx.fillStyle = (frameCount >> 4) & 1 ? '#58C4A0' : '#3C9E82';
      ctx.fillRect(0, groundY + 10, width, 2);
      ctx.fillStyle = '#33464E';
      ctx.fillRect(0, groundY + 24, width, 66);
      ctx.fillStyle = '#172329';
      ctx.fillRect(0, 166, width, height - 166);
    } else if (isBoarhold) {
      ctx.fillStyle = '#7A5B50';
      ctx.fillRect(0, groundY + 16, width, 70);
      ctx.fillStyle = '#A86E42';
      ctx.fillRect(0, groundY + 16, width, 3);
      ctx.fillRect(0, groundY + 84, width, 3);
      ctx.fillStyle = '#3A2828';
      ctx.fillRect(0, 166, width, height - 166);
    } else if (isDesert) {
      ctx.fillStyle = '#E8BE6E';
      ctx.fillRect(0, groundY + 10, width, 64);
      ctx.fillStyle = '#F6D892';
      ctx.fillRect(0, groundY + 18, width, 4);
      ctx.fillStyle = '#B87A3A';
      ctx.fillRect(0, 164, width, height - 164);
    } else if (isParadise) {
      ctx.fillStyle = '#48C4DC';
      ctx.fillRect(0, groundY, width, 18);
      ctx.fillStyle = '#F4E2B6';
      ctx.fillRect(0, groundY + 18, width, 68);
      ctx.fillStyle = '#C9A874';
      ctx.fillRect(0, 164, width, height - 164);
    } else if (isShrine) {
      ctx.fillStyle = '#535E72';
      ctx.fillRect(0, groundY + 14, width, 74);
      ctx.strokeStyle = (frameCount >> 4) & 1 ? '#5AD2F4' : '#A6D7F2';
      ctx.lineWidth = 1;
      ctx.beginPath();
      ctx.ellipse(width * 0.5, 128, 76, 26, 0, 0, Math.PI * 2);
      ctx.stroke();
      ctx.fillStyle = '#2B3140';
      ctx.fillRect(0, 168, width, height - 168);
    } else {
      ctx.fillStyle = '#74B461';
      ctx.fillRect(0, groundY + 4, width, 16);
      ctx.fillStyle = '#C89B62';
      ctx.fillRect(0, 102, width, 54);
      ctx.fillStyle = '#DFBA84';
      ctx.fillRect(0, 106, width, 4);
      ctx.fillRect(0, 148, width, 3);
      ctx.fillStyle = '#3B7542';
      ctx.fillRect(0, 162, width, height - 162);
    }

    // 3. Draw Rowan in Rear Support Stance when a Familiar is active!
    if (battle.activeSlot !== 0) {
      const rowanImg = atlas.getSprite('hero', 3, (frameCount >> 5) & 1);
      this.drawShadow(ctx, battle.wizardBackX, battle.wizardBackY, 14);
      ctx.drawImage(
        rowanImg,
        Math.round(battle.wizardBackX - rowanImg.width / 2),
        Math.round(battle.wizardBackY - rowanImg.height + 2)
      );
      // Pip hopping cheerfully on the ground behind Rowan (Mr. Drippy style)!
      const hopPhase = (frameCount % 44) / 44;
      const pipHop = hopPhase < 0.36 ? Math.round(Math.sin((hopPhase / 0.36) * Math.PI) * 6) : 0;
      const pipFrame = hopPhase >= 0.36 && hopPhase < 0.48 ? 2 : pipHop >= 2 ? 1 : 0;
      const pipImg = atlas.getSprite('pip', 3, pipFrame);
      this.drawShadow(ctx, battle.wizardBackX - 16, battle.wizardBackY + 1, 10);
      ctx.drawImage(
        pipImg,
        Math.round(battle.wizardBackX - 24),
        Math.round(battle.wizardBackY - 14 - pipHop)
      );
    }

    // 4. Soul-Arc Light Ribbon during Ni No Kuni Familiar Hot-Swap!
    if (battle.phase === 'SWAP_ARC') {
      const t = battle.swapArcProgress;
      ctx.save();
      ctx.strokeStyle = '#F5B841';
      ctx.lineWidth = 2;
      ctx.beginPath();
      ctx.moveTo(battle.wizardBackX + 4, battle.wizardBackY - 12);
      const midX = (battle.wizardBackX + battle.heroX) * 0.5;
      const midY = Math.min(battle.wizardBackY, battle.heroY) - 42;
      ctx.quadraticCurveTo(midX, midY, battle.heroX, battle.heroY - 12);
      ctx.stroke();

      // Luminous traveling soul-orb along the quadratic curve
      const inv = 1 - t;
      const orbX = inv * inv * (battle.wizardBackX + 4) + 2 * inv * t * midX + t * t * battle.heroX;
      const orbY = inv * inv * (battle.wizardBackY - 12) + 2 * inv * t * midY + t * t * (battle.heroY - 12);
      ctx.fillStyle = '#FFF6CC';
      ctx.fillRect(Math.round(orbX - 4), Math.round(orbY - 4), 8, 8);
      ctx.fillStyle = '#5AD2F4';
      ctx.fillRect(Math.round(orbX - 2), Math.round(orbY - 2), 4, 4);
      ctx.restore();
    }

    // 5. Active Frontline Party Combatant (Hero or Familiar)
    const activeMember = party[battle.activeSlot];
    if (battle.phase !== 'SWAP_ARC' || battle.swapArcProgress > 0.72) {
      const sprImg = atlas.getSprite(
        activeMember.spriteId,
        activeMember.slot === 0 ? 3 : 0,
        (frameCount >> 4) & 1
      );
      this.drawShadow(ctx, battle.heroX, battle.heroY, 18);

      // Guarding aura shield ring
      if (battle.heroGuarding) {
        ctx.strokeStyle = '#5AD2F4';
        ctx.lineWidth = 1;
        ctx.beginPath();
        ctx.ellipse(battle.heroX, battle.heroY - 10, 14, 15, 0, 0, Math.PI * 2);
        ctx.stroke();
      }

      this.drawScaledSprite(
        ctx,
        sprImg,
        battle.heroX,
        battle.heroY,
        battle.heroScaleXFp,
        battle.heroScaleYFp,
        false,
        battle.heroFlashFrames > 0
      );
    }

    // 6. Living Enemies + Target Reticle + Spell Locks
    battle.enemies.forEach((enemy, idx) => {
      if (!enemy.alive) return;
      this.drawBattleEnemy(
        ctx,
        enemy,
        idx === battle.selectedEnemy && battle.phase === 'COMMAND',
        atlas,
        frameCount
      );
    });

    // 6.5. Sea-of-Stars Choreographed Attack & Parry VFX
    this.renderBattleVfx(ctx, packet);

    // 7. Battle Screen-Space Particles
    this.renderParticles(ctx, packet, true);

    // 8. Diegetic Sea of Stars-Style Timed Hit / Parry Convergence Ring!
    if (battle.timedRing.active) {
      const tr = battle.timedRing;
      const isParry = tr.kind === 'PARRY';
      const inSweet = tr.progress >= tr.sweetStart && tr.progress <= tr.sweetEnd;
      const baseR = 10;
      const shrinkR = Math.max(baseR - 2, Math.round(baseR + (1 - Math.min(1.06, tr.progress)) * 26));

      ctx.save();
      if (inSweet) {
        ctx.fillStyle = isParry ? 'rgba(90, 210, 244, 0.25)' : 'rgba(245, 184, 65, 0.28)';
        ctx.beginPath();
        ctx.arc(tr.x, tr.y, baseR + 2, 0, Math.PI * 2);
        ctx.fill();
      }
      // Target inner anchor circle
      ctx.strokeStyle = isParry ? 'rgba(90, 210, 244, 0.9)' : 'rgba(245, 184, 65, 0.9)';
      ctx.lineWidth = 1.5;
      ctx.beginPath();
      ctx.arc(tr.x, tr.y, baseR, 0, Math.PI * 2);
      ctx.stroke();

      // Contracting timing ring (blazes bright inside sweet spot!)
      ctx.strokeStyle =
        tr.result === 'PERFECT'
          ? '#FFFFFF'
          : inSweet
            ? isParry
              ? '#5AD2F4'
              : '#FFF1A8'
            : isParry
              ? '#3B82B8'
              : '#D4942A';
      ctx.lineWidth = inSweet ? 2.5 : 1.5;
      ctx.beginPath();
      ctx.arc(tr.x, tr.y, shrinkR, 0, Math.PI * 2);
      ctx.stroke();

      // Micro cue prompt when inside sweet spot
      if (inSweet && tr.result === 'NONE') {
        ctx.font = 'bold 8px monospace';
        const cue = isParry ? 'PARRY!' : 'STRIKE!';
        const cw = ctx.measureText(cue).width;
        ctx.fillStyle = '#141824';
        ctx.fillRect(Math.round(tr.x - cw / 2 - 4), Math.round(tr.y - 24), Math.ceil(cw + 8), 11);
        ctx.strokeStyle = isParry ? '#5AD2F4' : '#FFF1A8';
        ctx.lineWidth = 1;
        ctx.strokeRect(Math.round(tr.x - cw / 2 - 4) + 0.5, Math.round(tr.y - 24) + 0.5, Math.ceil(cw + 7), 10);
        ctx.fillStyle = isParry ? '#5AD2F4' : '#FFF1A8';
        ctx.textBaseline = 'top';
        ctx.fillText(cue, Math.round(tr.x - cw / 2), Math.round(tr.y - 22));
      }
      ctx.restore();
    }

    // 9. Floating Damage / Critical / Lock-Break Numbers
    this.renderFloatingTexts(ctx, packet, false);

    // 10. Top Center Crisp Pixel Banner (e.g. "SOLAR SLAM!" / "BRIAR WARDEN!")
    if (battle.bannerFrames > 0 && battle.bannerText) {
      ctx.font = 'bold 8px monospace';
      const bw = Math.ceil(ctx.measureText(battle.bannerText).width) + 16;
      const bx = Math.round((width - bw) / 2);
      ctx.fillStyle = 'rgba(20, 24, 38, 0.92)';
      ctx.fillRect(bx, 6, bw, 14);
      ctx.strokeStyle = '#F5B841';
      ctx.lineWidth = 1;
      ctx.strokeRect(bx + 0.5, 6.5, bw - 1, 13);
      ctx.fillStyle = '#FFF6CC';
      ctx.textBaseline = 'top';
      ctx.fillText(battle.bannerText, bx + 8, 9);
    }
  }

  private renderBattleVfx(ctx: CanvasRenderingContext2D, packet: PpuFramePacket): void {
    const v = packet.battle.vfx;
    if (v.kind === 'NONE' || v.dur <= 0) return;
    const p = Math.max(0, Math.min(1, (v.dur - v.timer) / v.dur));
    const cx = v.x + (v.tx - v.x) * p;
    const cy = v.y + (v.ty - v.y) * p;

    ctx.save();
    switch (v.kind) {
      case 'STAFF_ARC': {
        const arcY = cy - Math.sin(p * Math.PI) * 16;
        ctx.strokeStyle = '#FFF6CC';
        ctx.lineWidth = 2;
        ctx.beginPath();
        ctx.arc(cx, arcY, 10 + p * 8, -Math.PI * 0.75, Math.PI * 0.25);
        ctx.stroke();
        ctx.strokeStyle = '#F5B841';
        ctx.lineWidth = 1.5;
        ctx.beginPath();
        ctx.arc(cx, arcY, 7 + p * 6, -Math.PI * 0.65, Math.PI * 0.35);
        ctx.stroke();
        break;
      }
      case 'ACORN_SPIN': {
        const ang = p * Math.PI * 6;
        ctx.translate(cx, cy);
        ctx.rotate(ang);
        ctx.strokeStyle = '#8CD674';
        ctx.lineWidth = 2;
        ctx.beginPath();
        ctx.arc(0, 0, 12, 0, Math.PI * 1.5);
        ctx.stroke();
        ctx.strokeStyle = '#FFF1A8';
        ctx.lineWidth = 1.5;
        ctx.beginPath();
        ctx.moveTo(-11, 0);
        ctx.lineTo(11, 0);
        ctx.moveTo(0, -11);
        ctx.lineTo(0, 11);
        ctx.stroke();
        break;
      }
      case 'SOLAR_METEOR': {
        const my = cy - Math.sin(p * Math.PI) * 52;
        ctx.fillStyle = 'rgba(232, 106, 74, 0.45)';
        ctx.beginPath();
        ctx.arc(cx, my, 15, 0, Math.PI * 2);
        ctx.fill();
        ctx.fillStyle = '#FFE066';
        ctx.beginPath();
        ctx.arc(cx, my, 8, 0, Math.PI * 2);
        ctx.fill();
        ctx.strokeStyle = '#FFF6CC';
        ctx.lineWidth = 1.5;
        ctx.beginPath();
        ctx.arc(cx, my, 17 + (packet.frameCount & 3), 0, Math.PI * 2);
        ctx.stroke();
        if (p > 0.8) {
          const shock = (p - 0.8) * 140;
          ctx.strokeStyle = '#FFE066';
          ctx.lineWidth = 2;
          ctx.beginPath();
          ctx.ellipse(v.tx, v.ty + 4, shock + 6, shock * 0.45 + 3, 0, 0, Math.PI * 2);
          ctx.stroke();
        }
        break;
      }
      case 'STAR_BOLT': {
        const by = cy - Math.sin(p * Math.PI) * 28;
        ctx.strokeStyle = 'rgba(90, 210, 244, 0.7)';
        ctx.lineWidth = 2;
        ctx.beginPath();
        ctx.moveTo(v.x, v.y);
        ctx.quadraticCurveTo((v.x + v.tx) * 0.5, Math.min(v.y, v.ty) - 34, cx, by);
        ctx.stroke();
        ctx.fillStyle = 'rgba(90, 210, 244, 0.45)';
        ctx.beginPath();
        ctx.arc(cx, by, 9, 0, Math.PI * 2);
        ctx.fill();
        ctx.fillStyle = '#FFFFFF';
        ctx.fillRect(Math.round(cx - 3), Math.round(by - 3), 6, 6);
        ctx.strokeStyle = '#FFF6CC';
        ctx.lineWidth = 1;
        ctx.beginPath();
        ctx.moveTo(cx - 9, by);
        ctx.lineTo(cx + 9, by);
        ctx.moveTo(cx, by - 9);
        ctx.lineTo(cx, by + 9);
        ctx.stroke();
        break;
      }
      case 'PRISM_NOVA': {
        const r1 = 8 + p * 34;
        const r2 = 4 + p * 22;
        ctx.fillStyle = `rgba(90, 210, 244, ${(0.35 * (1 - p)).toFixed(2)})`;
        ctx.beginPath();
        ctx.arc(v.tx, v.ty, r2, 0, Math.PI * 2);
        ctx.fill();
        ctx.strokeStyle = '#FFF1A8';
        ctx.lineWidth = 1.5;
        ctx.beginPath();
        ctx.arc(v.tx, v.ty, r1, 0, Math.PI * 2);
        ctx.stroke();
        ctx.strokeStyle = '#5AD2F4';
        ctx.beginPath();
        ctx.moveTo(v.tx - r1, v.ty);
        ctx.lineTo(v.tx + r1, v.ty);
        ctx.moveTo(v.tx, v.ty - r1);
        ctx.lineTo(v.tx, v.ty + r1);
        ctx.stroke();
        break;
      }
      case 'ENEMY_CLAW': {
        ctx.strokeStyle = '#E85272';
        ctx.lineWidth = 2;
        for (let k = -1; k <= 1; k++) {
          ctx.beginPath();
          ctx.moveTo(cx - 10, cy - 8 + k * 4);
          ctx.lineTo(cx + 10, cy + 8 + k * 4);
          ctx.stroke();
        }
        break;
      }
      case 'GUARD_SHIELD': {
        const r = 14 + p * 10;
        ctx.fillStyle = `rgba(90, 210, 244, ${(0.38 * (1 - p * 0.5)).toFixed(2)})`;
        ctx.beginPath();
        ctx.ellipse(v.x, v.y, r - 2, r + 2, 0, 0, Math.PI * 2);
        ctx.fill();
        ctx.strokeStyle = '#FFFFFF';
        ctx.lineWidth = 2;
        ctx.beginPath();
        ctx.ellipse(v.x, v.y, r, r + 3, 0, 0, Math.PI * 2);
        ctx.stroke();
        break;
      }
      case 'HEAL_BLOOM': {
        const r = 10 + p * 20;
        ctx.fillStyle = `rgba(116, 214, 128, ${(0.32 * (1 - p)).toFixed(2)})`;
        ctx.beginPath();
        ctx.ellipse(v.x, v.y, r, r * 0.55, 0, 0, Math.PI * 2);
        ctx.fill();
        ctx.strokeStyle = '#FFF1A8';
        ctx.lineWidth = 1.5;
        ctx.beginPath();
        ctx.ellipse(v.x, v.y - p * 18, r * 0.7, r * 0.35, 0, 0, Math.PI * 2);
        ctx.stroke();
        break;
      }
      default:
        break;
    }
    ctx.restore();
  }

  private drawBattleEnemy(
    ctx: CanvasRenderingContext2D,
    enemy: BattleEnemyState,
    isTargeted: boolean,
    atlas: PixelAtlas,
    frameCount: number
  ): void {
    const sprImg = atlas.getSprite(enemy.spriteId, 0, (enemy.animFrame >> 4) & 1);
    const shadowW = enemy.isBoss ? 30 : 18;
    this.drawShadow(ctx, enemy.x, enemy.y, shadowW);

    const recoilX = enemy.hurtFrames > 0 ? ((enemy.hurtFrames & 2) ? 2 : -2) : 0;
    this.drawScaledSprite(
      ctx,
      sprImg,
      enemy.x + recoilX,
      enemy.y,
      enemy.scaleXFp,
      enemy.scaleYFp,
      false,
      enemy.flashFrames > 0
    );

    const topY = enemy.y - sprImg.height - 4;

    // Compact enemy HP bar
    const barW = enemy.isBoss ? 36 : 24;
    const barX = Math.round(enemy.x - barW / 2);
    const hpRatio = Math.max(0, enemy.hp / enemy.maxHp);
    ctx.fillStyle = '#181C28';
    ctx.fillRect(barX - 1, enemy.y + 4, barW + 2, 5);
    ctx.fillStyle = enemy.staggered ? '#F5B841' : '#E04E4E';
    ctx.fillRect(barX, enemy.y + 5, Math.round(barW * hpRatio), 3);

    // Crisp Target Selector Pointer
    if (isTargeted) {
      const bob = Math.round(Math.sin(frameCount * 0.2) * 2);
      const py = topY - (enemy.locks.length > 0 ? 15 : 4) + bob;
      ctx.fillStyle = '#181C28';
      ctx.fillRect(enemy.x - 4, py - 4, 8, 6);
      ctx.fillStyle = '#F5B841';
      ctx.fillRect(enemy.x - 3, py - 3, 6, 2);
      ctx.fillRect(enemy.x - 2, py - 1, 4, 2);
      ctx.fillRect(enemy.x - 1, py + 1, 2, 1);
    }

    // Sea of Stars-Style Spell Lock Icons above charging enemies!
    if (enemy.chargeTurns > 0 && enemy.locks.length > 0) {
      const lockSpacing = 12;
      const totalW = enemy.locks.length * lockSpacing;
      const startX = Math.round(enemy.x - totalW / 2 + 1);
      const ly = topY - 10;

      // Countdown turns pip
      ctx.fillStyle = '#191421';
      ctx.fillRect(startX - 11, ly, 10, 10);
      ctx.fillStyle = '#E04E4E';
      ctx.font = 'bold 8px monospace';
      ctx.textBaseline = 'top';
      ctx.fillText(String(enemy.chargeTurns), startX - 8, ly + 1);

      enemy.locks.forEach((lk, i) => {
        const lx = startX + i * lockSpacing;
        const broken = enemy.brokenLocks[i];
        this.drawLockIcon(ctx, lx, ly, lk, broken);
      });
    }
  }

  private drawLockIcon(
    ctx: CanvasRenderingContext2D,
    x: number,
    y: number,
    kind: LockKind,
    broken: boolean
  ): void {
    ctx.fillStyle = broken ? 'rgba(28, 32, 46, 0.5)' : '#1C2234';
    ctx.fillRect(x, y, 10, 10);
    ctx.strokeStyle = broken
      ? '#475066'
      : kind === 'SWORD'
        ? '#E8DEC8'
        : kind === 'SUN'
          ? '#F5B841'
          : '#5AD2F4';
    ctx.lineWidth = 1;
    ctx.strokeRect(x + 0.5, y + 0.5, 9, 9);

    if (broken) {
      ctx.fillStyle = '#525C73';
      ctx.fillRect(x + 2, y + 4, 6, 1);
      return;
    }

    if (kind === 'SWORD') {
      ctx.fillStyle = '#EAE0C8';
      ctx.fillRect(x + 6, y + 2, 2, 2);
      ctx.fillRect(x + 5, y + 3, 2, 2);
      ctx.fillRect(x + 4, y + 4, 2, 2);
      ctx.fillStyle = '#E04E4E';
      ctx.fillRect(x + 2, y + 6, 3, 2);
    } else if (kind === 'SUN') {
      ctx.fillStyle = '#F5B841';
      ctx.fillRect(x + 3, y + 3, 4, 4);
      ctx.fillStyle = '#FFF6CC';
      ctx.fillRect(x + 4, y + 4, 2, 2);
      ctx.fillRect(x + 4, y + 1, 2, 1);
      ctx.fillRect(x + 4, y + 8, 2, 1);
      ctx.fillRect(x + 1, y + 4, 1, 2);
      ctx.fillRect(x + 8, y + 4, 1, 2);
    } else {
      ctx.fillStyle = '#5AD2F4';
      ctx.fillRect(x + 4, y + 2, 2, 6);
      ctx.fillRect(x + 2, y + 4, 6, 2);
      ctx.fillStyle = '#FFFFFF';
      ctx.fillRect(x + 4, y + 4, 2, 2);
    }
  }

  /* ========================================================================
   * OAM SPRITE & PARTICLE HELPERS
   * ======================================================================== */
  private drawShadow(ctx: CanvasRenderingContext2D, cx: number, cy: number, w: number): void {
    ctx.fillStyle = 'rgba(18, 22, 32, 0.36)';
    ctx.fillRect(Math.round(cx - w / 2), Math.round(cy - 2), w, 4);
    ctx.fillRect(Math.round(cx - w / 2 + 2), Math.round(cy - 3), w - 4, 6);
  }

  private drawOamSprite(
    ctx: CanvasRenderingContext2D,
    spr: OamSprite,
    atlas: PixelAtlas,
    frameCount: number
  ): void {
    const img = atlas.getSprite(spr.spriteId, spr.dir, spr.frame);

    // Radiant or Hollow ground aura ring for Heart-Mending NPCs!
    if (spr.aura === 'RADIANT_COURAGE' || spr.aura === 'HOLLOW_FEAR' || spr.aura === 'MENDED') {
      const pulse = Math.sin(frameCount * 0.1) * 1.5;
      ctx.save();
      ctx.strokeStyle =
        spr.aura === 'HOLLOW_FEAR'
          ? 'rgba(158, 118, 204, 0.85)'
          : spr.aura === 'RADIANT_COURAGE'
            ? 'rgba(245, 184, 65, 0.9)'
            : 'rgba(116, 180, 97, 0.55)';
      ctx.lineWidth = 1;
      ctx.beginPath();
      ctx.ellipse(spr.x, spr.y - 1, 10 + pulse, 5 + pulse * 0.5, 0, 0, Math.PI * 2);
      ctx.stroke();
      ctx.restore();
    }

    if (spr.shadow) {
      const sw =
        spr.spriteId === 'boss_briar_warden' ||
        spr.spriteId === 'boss_muckmaw' ||
        spr.spriteId === 'boss_ironfang'
          ? 24
          : spr.spriteId === 'hollow_oak'
            ? 20
            : spr.spriteId === 'pip' || spr.spriteId === 'pet_milo' || spr.spriteId === 'bobber'
              ? 8
              : 12;
      const shadowY = spr.spriteId === 'pip' ? spr.sortY : spr.y;
      this.drawShadow(ctx, spr.x, shadowY, sw);
    }

    if (spr.spriteId === 'pip') {
      ctx.save();
      ctx.fillStyle = 'rgba(255, 224, 128, 0.22)';
      ctx.beginPath();
      ctx.ellipse(spr.x, spr.y - 4, 7, 6, 0, 0, Math.PI * 2);
      ctx.fill();
      ctx.restore();
    }

    if (spr.rotationDeg !== 0) {
      ctx.save();
      ctx.translate(spr.x, spr.y);
      ctx.rotate((spr.rotationDeg * Math.PI) / 180);
      ctx.drawImage(img, -Math.round(img.width / 2), -Math.round(img.height / 2));
      ctx.restore();
      return;
    }

    this.drawScaledSprite(
      ctx,
      img,
      spr.x,
      spr.y,
      spr.scaleXFp,
      spr.scaleYFp,
      spr.flipX,
      spr.flashWhite
    );
  }

  private drawScaledSprite(
    ctx: CanvasRenderingContext2D,
    img: HTMLCanvasElement | HTMLImageElement,
    anchorX: number,
    anchorY: number,
    scaleXFp: number,
    scaleYFp: number,
    flipX: boolean,
    flashWhite: boolean
  ): void {
    const w = img.width || 16;
    const h = img.height || 16;
    const drawW = Math.max(4, Math.round((w * scaleXFp) / FP_ONE));
    const drawH = Math.max(4, Math.round((h * scaleYFp) / FP_ONE));

    let source: HTMLCanvasElement | HTMLImageElement = img;
    if (flashWhite) {
      // Crisp 16-bit hit-flash silhouette!
      const sw = Math.min(48, w);
      const sh = Math.min(48, h);
      this.scratchCtx.clearRect(0, 0, 48, 48);
      this.scratchCtx.globalCompositeOperation = 'source-over';
      this.scratchCtx.drawImage(img, 0, 0, sw, sh);
      this.scratchCtx.globalCompositeOperation = 'source-in';
      this.scratchCtx.fillStyle = '#FFFFFF';
      this.scratchCtx.fillRect(0, 0, sw, sh);
      this.scratchCtx.globalCompositeOperation = 'source-over';
      source = this.scratchCanvas;
    }

    ctx.save();
    ctx.translate(Math.round(anchorX), Math.round(anchorY));
    if (flipX) ctx.scale(-1, 1);
    if (flashWhite) {
      ctx.drawImage(source, 0, 0, Math.min(48, w), Math.min(48, h), -Math.round(drawW / 2), -drawH + 2, drawW, drawH);
    } else {
      ctx.drawImage(source, -Math.round(drawW / 2), -drawH + 2, drawW, drawH);
    }
    ctx.restore();
  }

  private renderParticles(
    ctx: CanvasRenderingContext2D,
    packet: PpuFramePacket,
    battleOnly: boolean
  ): void {
    for (const p of packet.particles) {
      if (!p.active) continue;
      if (p.screenSpace !== battleOnly) continue;

      const px = (p.fx >> 8) - (battleOnly ? 0 : packet.camX);
      const py = (p.fy >> 8) - (battleOnly ? 0 : packet.camY);
      const alpha = Math.min(1, p.life / Math.max(1, p.maxLife * 0.45));

      ctx.save();
      ctx.globalAlpha = alpha;
      ctx.fillStyle = p.color;
      ctx.fillRect(px, py, p.size, p.size);
      ctx.restore();
    }
  }

  private renderFloatingTexts(
    ctx: CanvasRenderingContext2D,
    packet: PpuFramePacket,
    worldOffset: boolean
  ): void {
    ctx.save();
    ctx.textBaseline = 'top';
    for (const ft of packet.floatingTexts) {
      if (!ft.active) continue;
      const sx = Math.round(ft.x - (worldOffset ? packet.camX : 0));
      const sy = Math.round(ft.y - (worldOffset ? packet.camY : 0));
      const fontSize = ft.scale > 1.1 ? 10 : 8;
      ctx.font = `bold ${fontSize}px monospace`;
      const tw = ctx.measureText(ft.text).width;
      const dx = Math.round(sx - tw / 2);

      // Crisp 1px pixel outline for instant legibility
      ctx.fillStyle = '#11131A';
      ctx.fillText(ft.text, dx - 1, sy);
      ctx.fillText(ft.text, dx + 1, sy);
      ctx.fillText(ft.text, dx, sy - 1);
      ctx.fillText(ft.text, dx, sy + 1);
      ctx.fillStyle = ft.color;
      ctx.fillText(ft.text, dx, sy);
    }
    ctx.restore();
  }
}
